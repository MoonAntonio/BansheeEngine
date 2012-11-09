#include "CmResources.h"
#include "CmResource.h"
#include "CmException.h"
#include "CmFileSerializer.h"
#include "CmFileSystem.h"
#include "CmUUID.h"
#include "CmPath.h"
#include "CmDebug.h"
#include "CmResourcesRTTI.h"

namespace CamelotEngine
{
	bool Resources::ResourceRequestHandler::canHandleRequest(const WorkQueue::Request* req, const WorkQueue* srcQ)
	{
		return true;
	}

	WorkQueue::Response* Resources::ResourceRequestHandler::handleRequest(const WorkQueue::Request* req, const WorkQueue* srcQ)
	{
		ResourceLoadRequestPtr resRequest = boost::any_cast<ResourceLoadRequestPtr>(req->getData());

		ResourceLoadResponsePtr resResponse = ResourceLoadResponsePtr(new Resources::ResourceLoadResponse());
		resResponse->rawResource = gResources().loadFromDiskAndDeserialize(resRequest->filePath);

		return new WorkQueue::Response(req, true, resResponse);
	}

	bool Resources::ResourceResponseHandler::canHandleResponse(const WorkQueue::Response* res, const WorkQueue* srcQ)
	{
		return true;
	}

	void Resources::ResourceResponseHandler::handleResponse(const WorkQueue::Response* res, const WorkQueue* srcQ)
	{
		ResourceLoadRequestPtr resRequest = boost::any_cast<ResourceLoadRequestPtr>(res->getRequest()->getData());

		auto iterFind = gResources().mInProgressResources.find(resRequest->filePath);
		gResources().mInProgressResources.erase(iterFind);

		if(res->getRequest()->getAborted())
			return;

		if(res->succeeded())
		{
			ResourceLoadResponsePtr resResponse = boost::any_cast<ResourceLoadResponsePtr>(res->getData());
			
			resResponse->rawResource->init();
			resRequest->resource.resolve(resResponse->rawResource);

			if(!gResources().metaExists_UUID(resResponse->rawResource->getUUID()))
			{
				gDebug().logWarning("Loading a resource that doesn't have meta-data. Creating meta-data automatically. Resource path: " + resRequest->filePath);
				gResources().addMetaData(resResponse->rawResource->getUUID(), resRequest->filePath);
			}

			gResources().mLoadedResources[resRequest->filePath] = resRequest->resource;
		}
		else
		{
			gDebug().logWarning("Resource load request failed.");
		}
	}

	Resources::Resources(const String& metaDataFolder)
		:mRequestHandler(nullptr), mResponseHandler(nullptr)
	{
		mMetaDataFolderPath = metaDataFolder;

		if(!FileSystem::dirExists(mMetaDataFolderPath))
		{
			FileSystem::createDir(mMetaDataFolderPath);
		}

		loadMetaData();

		mWorkQueue = WorkQueuePtr(new WorkQueue());
		mWorkQueueChannel = mWorkQueue->getChannel("Resources");
		mRequestHandler = new ResourceRequestHandler();
		mResponseHandler = new ResourceResponseHandler();

		mWorkQueue->addRequestHandler(mWorkQueueChannel, mRequestHandler);
		mWorkQueue->addResponseHandler(mWorkQueueChannel, mResponseHandler);

		// TODO Low priority - I might want to make this more global so other classes can use it
#if CM_THREAD_SUPPORT
		mWorkQueue->setWorkerThreadCount(CM_THREAD_HARDWARE_CONCURRENCY);
#endif
		mWorkQueue->startup();
	}

	Resources::~Resources()
	{
		if(mWorkQueue)
		{
			if(mRequestHandler != nullptr)
				mWorkQueue->removeRequestHandler(mWorkQueueChannel, mRequestHandler);

			if(mResponseHandler != nullptr)
				mWorkQueue->removeResponseHandler(mWorkQueueChannel, mResponseHandler);

			mWorkQueue->shutdown();
		}

		if(mRequestHandler != nullptr)
			delete mRequestHandler;

		if(mResponseHandler != nullptr)
			delete mResponseHandler;
	}

	void Resources::update()
	{
		mWorkQueue->processResponses();
	}

	BaseResourceRef Resources::load(const String& filePath)
	{
		return loadInternal(filePath, true);
	}

	BaseResourceRef Resources::loadAsync(const String& filePath)
	{
		return loadInternal(filePath, false);
	}

	BaseResourceRef Resources::loadFromUUID(const String& uuid)
	{
		if(!metaExists_UUID(uuid))
		{
			gDebug().logWarning("Cannot load resource. Resource with UUID '" + uuid + "' doesn't exist.");
			return nullptr;
		}

		ResourceMetaDataPtr metaEntry = mResourceMetaData[uuid];

		return load(metaEntry->mPath);
	}

	BaseResourceRef Resources::loadFromUUIDAsync(const String& uuid)
	{
		if(!metaExists_UUID(uuid))
		{
			gDebug().logWarning("Cannot load resource. Resource with UUID '" + uuid + "' doesn't exist.");
			return nullptr;
		}

		ResourceMetaDataPtr metaEntry = mResourceMetaData[uuid];

		return loadAsync(metaEntry->mPath);
	}

	BaseResourceRef Resources::loadInternal(const String& filePath, bool synchronous)
	{
		auto iterFind = mLoadedResources.find(filePath);
		if(iterFind != mLoadedResources.end()) // Resource is already loaded
		{
			return iterFind->second;
		}

		auto iterFind2 = mInProgressResources.find(filePath);
		if(iterFind2 != mInProgressResources.end()) // We're already loading this resource
		{
			ResourceAsyncOp& asyncOp = iterFind2->second;

			if(!synchronous)
				return asyncOp.resource;
			else
			{
				// It's already being loaded asynchronously but we want it right away,
				// so abort the async operation and load it right away.
				mWorkQueue->abortRequest(asyncOp.requestID);
			}
		}

		if(!FileSystem::fileExists(filePath))
		{
			gDebug().logWarning("Specified file: " + filePath + " doesn't exist.");
			return nullptr;
		}

		// TODO Low priority - Right now I don't allow loading of resources that don't have meta-data, because I need to know resources UUID
		// at this point. And I can't do that without having meta-data. Other option is to partially load the resource to read the UUID but due to the
		// nature of the serializer it could complicate things. (But possible if this approach proves troublesome)
		// The reason I need the UUID is that when resource is loaded Async, the returned ResourceRef needs to have a valid UUID, in case I assign that
		// ResourceRef to something and then save that something. If I didn't assign it, the saved ResourceRef would have a blank (i.e. invalid) UUID.
		if(!metaExists_Path(filePath))
		{
			CM_EXCEPT(InternalErrorException, "Cannot load resource that isn't registered in the meta database. Call Resources::create first.");
		}

		String uuid = getUUIDFromPath(filePath);
		BaseResourceRef newResource;
		newResource.setUUID(uuid); // UUID needs to be set immediately if the resource gets loaded async

		ResourceLoadRequestPtr resRequest = ResourceLoadRequestPtr(new Resources::ResourceLoadRequest());
		resRequest->filePath = filePath;
		resRequest->resource = newResource;

		WorkQueue::RequestID requestId = mWorkQueue->peekNextFreeRequestId();
		ResourceAsyncOp newAsyncOp;
		newAsyncOp.resource = newResource;
		newAsyncOp.requestID = requestId;

		mInProgressResources[filePath] = newAsyncOp;

		mWorkQueue->addRequest(mWorkQueueChannel, resRequest, 0, synchronous);
		return newResource;
	}

	ResourcePtr Resources::loadFromDiskAndDeserialize(const String& filePath)
	{
		FileSerializer fs;
		std::shared_ptr<IReflectable> loadedData = fs.decode(filePath);

		if(loadedData == nullptr)
			CM_EXCEPT(InternalErrorException, "Unable to load resource.");

		if(!loadedData->isDerivedFrom(Resource::getRTTIStatic()))
			CM_EXCEPT(InternalErrorException, "Loaded class doesn't derive from Resource.");

		ResourcePtr resource = std::static_pointer_cast<Resource>(loadedData);
		return resource;
	}

	void Resources::create(BaseResourceRef resource, const String& filePath, bool overwrite)
	{
		assert(resource.get() != nullptr);

		if(metaExists_UUID(resource->getUUID()))
			CM_EXCEPT(InvalidParametersException, "Specified resource already exists.");
		
		bool fileExists = FileSystem::fileExists(filePath);
		const String& existingUUID = getUUIDFromPath(filePath);
		bool metaExists = metaExists_UUID(existingUUID);

		if(fileExists)
		{
			if(overwrite)
				FileSystem::remove(filePath);
			else
				CM_EXCEPT(InvalidParametersException, "Another file exists at the specified location.");
		}

		if(metaExists)
			removeMetaData(existingUUID);

		addMetaData(resource->getUUID(), filePath);
		save(resource);
	}

	void Resources::save(BaseResourceRef resource)
	{
		assert(resource.get() != nullptr);

		if(!metaExists_UUID(resource->getUUID()))
			CM_EXCEPT(InvalidParametersException, "Cannot find resource meta-data. Please call Resources::create before trying to save the resource.");

		const String& filePath = getPathFromUUID(resource->getUUID());

		FileSerializer fs;
		fs.encode(resource.get(), filePath);
	}

	void Resources::loadMetaData()
	{
		vector<String>::type allFiles = FileSystem::getFiles(mMetaDataFolderPath);

		for(auto iter = allFiles.begin(); iter != allFiles.end(); ++iter)
		{
			String& path = *iter;
			if(Path::hasExtension(path, "resmeta"))
			{
				FileSerializer fs;
				std::shared_ptr<IReflectable> loadedData = fs.decode(path);

				ResourceMetaDataPtr metaData = std::static_pointer_cast<ResourceMetaData>(loadedData);
				mResourceMetaData[metaData->mUUID] = metaData;
				mResourceMetaData_FilePath[metaData->mPath] = metaData;
			}
		}
	}

	void Resources::saveMetaData(const ResourceMetaDataPtr metaData)
	{
		String fullPath = Path::combine(mMetaDataFolderPath, metaData->mUUID + ".resmeta");

		FileSerializer fs;
		fs.encode(metaData.get(), fullPath);
	}

	void Resources::removeMetaData(const String& uuid)
	{
		String fullPath = Path::combine(mMetaDataFolderPath, uuid + ".resmeta");
		FileSystem::remove(fullPath);

		auto iter = mResourceMetaData.find(uuid);

		if(iter != mResourceMetaData.end())
		{
			mResourceMetaData.erase(iter);
			mResourceMetaData_FilePath.erase(iter->second->mPath);
		}
		else
			gDebug().logWarning("Trying to remove meta data that doesn't exist.");
	}

	void Resources::addMetaData(const String& uuid, const String& filePath)
	{
		if(metaExists_Path(filePath))
			CM_EXCEPT(InvalidParametersException, "Resource with the path '" + filePath + "' already exists.");

		if(metaExists_UUID(uuid))
			CM_EXCEPT(InternalErrorException, "Resource with the same UUID already exists. UUID: " + uuid);

		ResourceMetaDataPtr dbEntry(new ResourceMetaData());
		dbEntry->mPath = filePath;
		dbEntry->mUUID = uuid;

		mResourceMetaData[uuid] = dbEntry;
		mResourceMetaData_FilePath[filePath] = dbEntry;

		saveMetaData(dbEntry);
	}

	void Resources::updateMetaData(const String& uuid, const String& newFilePath)
	{
		if(!metaExists_UUID(uuid))
		{
			CM_EXCEPT(InvalidParametersException, "Cannot update a resource that doesn't exist. UUID: " + uuid + ". File path: " + newFilePath);
		}

		ResourceMetaDataPtr dbEntry = mResourceMetaData[uuid];
		dbEntry->mPath = newFilePath;

		saveMetaData(dbEntry);
	}

	const String& Resources::getPathFromUUID(const String& uuid) const
	{
		auto findIter = mResourceMetaData.find(uuid);

		if(findIter != mResourceMetaData.end())
			return findIter->second->mPath;
		else
			return StringUtil::BLANK;
	}

	const String& Resources::getUUIDFromPath(const String& path) const
	{
		auto findIter = mResourceMetaData_FilePath.find(path);

		if(findIter != mResourceMetaData_FilePath.end())
			return findIter->second->mUUID;
		else
			return StringUtil::BLANK;
	}

	bool Resources::metaExists_UUID(const String& uuid) const
	{
		auto findIter = mResourceMetaData.find(uuid);

		return findIter != mResourceMetaData.end();
	}

	bool Resources::metaExists_Path(const String& path) const
	{
		auto findIter = mResourceMetaData_FilePath.find(path);

		return findIter != mResourceMetaData_FilePath.end();
	}

	CM_EXPORT Resources& gResources()
	{
		return Resources::instance();
	}

	/************************************************************************/
	/* 								SERIALIZATION                      		*/
	/************************************************************************/
	RTTITypeBase* Resources::ResourceMetaData::getRTTIStatic()
	{
		return ResourceMetaDataRTTI::instance();
	}

	RTTITypeBase* Resources::ResourceMetaData::getRTTI() const
	{
		return Resources::ResourceMetaData::getRTTIStatic();
	}
}