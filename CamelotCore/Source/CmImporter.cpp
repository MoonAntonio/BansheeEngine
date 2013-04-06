#include "CmImporter.h"
#include "CmPath.h"
#include "CmResource.h"
#include "CmFileSystem.h"
#include "CmSpecificImporter.h"
#include "CmGpuProgIncludeImporter.h"
#include "CmGpuProgramImporter.h"
#include "CmImportOptions.h"
#include "CmDebug.h"
#include "CmDataStream.h"
#include "CmException.h"

namespace CamelotEngine
{
	Importer::Importer()
	{
		registerAssetImporter(CM_NEW(GpuProgIncludeImporter, GenAlloc) GpuProgIncludeImporter());
		registerAssetImporter(CM_NEW(GpuProgramImporter, GenAlloc) GpuProgramImporter());
	}

	Importer::~Importer()
	{
		for(auto i = mAssetImporters.begin(); i != mAssetImporters.end(); ++i)
		{
			if((*i) != nullptr)
				CM_DELETE(*i, SpecificImporter, GenAlloc);
		}

		mAssetImporters.clear();
	}

	bool Importer::supportsFileType(const String& extension) const
	{
		for(auto iter = mAssetImporters.begin(); iter != mAssetImporters.end(); ++iter)
		{
			if(*iter != nullptr && (*iter)->isExtensionSupported(extension))
				return true;
		}

		return false;
	}

	bool Importer::supportsFileType(const UINT8* magicNumber, UINT32 magicNumSize) const
	{
		for(auto iter = mAssetImporters.begin(); iter != mAssetImporters.end(); ++iter)
		{
			if(*iter != nullptr && (*iter)->isMagicNumberSupported(magicNumber, magicNumSize))
				return true;
		}

		return false;
	}

	HResource Importer::import(const String& inputFilePath, ConstImportOptionsPtr importOptions)
	{
		if(!FileSystem::fileExists(inputFilePath))
		{
			LOGWRN("Trying to import asset that doesn't exists. Asset path: " + inputFilePath);
			return HResource();
		}

		SpecificImporter* importer = getImporterForFile(inputFilePath);
		if(importer == nullptr)
			return HResource();

		if(importOptions == nullptr)
			importOptions = importer->getDefaultImportOptions();
		else
		{
			ConstImportOptionsPtr defaultImportOptions = importer->getDefaultImportOptions();
			if(importOptions->getTypeId() != defaultImportOptions->getTypeId())
			{
				CM_EXCEPT(InvalidParametersException, "Provided import options is not of valid type. " \
					"Expected: " + defaultImportOptions->getTypeName() + ". Got: " + importOptions->getTypeName() + ".");
			}
		}

		HResource importedResource = importer->import(inputFilePath, importOptions);

		return importedResource;
	}

	ImportOptionsPtr Importer::createImportOptions(const String& inputFilePath)
	{
		if(!FileSystem::fileExists(inputFilePath))
		{
			LOGWRN("Trying to import asset that doesn't exists. Asset path: " + inputFilePath);
			return nullptr;
		}

		SpecificImporter* importer = getImporterForFile(inputFilePath);
		if(importer == nullptr)
			return nullptr;

		return importer->createImportOptions();
	}

	void Importer::registerAssetImporter(SpecificImporter* importer)
	{
		if(!importer)
		{
			LOGWRN("Trying to register a null asset importer!");
			return;
		}

		mAssetImporters.push_back(importer);
	}

	SpecificImporter* Importer::getImporterForFile(const String& inputFilePath) const
	{
		String ext = Path::getExtension(inputFilePath);
		ext = ext.substr(1, ext.size() - 1); // Remove the .
		if(!supportsFileType(ext))
		{
			LOGWRN("There is no importer for the provided file type. (" + inputFilePath + ")");
			return nullptr;
		}

		for(auto iter = mAssetImporters.begin(); iter != mAssetImporters.end(); ++iter)
		{
			if(*iter != nullptr && (*iter)->isExtensionSupported(ext))
			{
				return *iter;
			}
		}

		return nullptr;
	}
}