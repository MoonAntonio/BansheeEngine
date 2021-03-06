//********************************** Banshee Engine (www.banshee3d.com) **************************************************//
//**************** Copyright (c) 2016 Marko Pintera (marko.pintera@gmail.com). All rights reserved. **********************//
#include "Serialization/BsScriptAssemblyManager.h"
#include "Serialization/BsManagedSerializableObjectInfo.h"
#include "BsMonoManager.h"
#include "BsMonoAssembly.h"
#include "BsMonoClass.h"
#include "BsMonoField.h"
#include "BsMonoMethod.h"
#include "BsMonoProperty.h"
#include "Wrappers/BsScriptManagedResource.h"
#include "Wrappers/BsScriptComponent.h"
#include "BsBuiltinComponentLookup.generated.h"

// Note: This resource registration code is only here because resource lookup auto-generation isn't yet hooked up
#include "Image/BsSpriteTexture.h"
#include "Mesh/BsMesh.h"
#include "Text/BsFont.h"
#include "Material/BsShader.h"
#include "Material/BsShaderInclude.h"
#include "Material/BsMaterial.h"
#include "Scene/BsPrefab.h"
#include "Resources/BsPlainText.h"
#include "Resources/BsScriptCode.h"
#include "Localization/BsStringTable.h"
#include "GUI/BsGUISkin.h"
#include "Physics/BsPhysicsMaterial.h"
#include "Physics/BsPhysicsMesh.h"
#include "Audio/BsAudioClip.h"
#include "Animation/BsAnimationClip.h"

#include "BsScriptTexture.generated.h"
#include "Wrappers/BsScriptPlainText.h"
#include "Wrappers/BsScriptScriptCode.h"
#include "Wrappers/BsScriptShaderInclude.h"
#include "Wrappers/BsScriptPrefab.h"
#include "Wrappers/GUI/BsScriptGUISkin.h"
#include "Serialization/BsBuiltinResourceLookup.h"

#include "BsScriptMesh.generated.h"
#include "BsScriptPhysicsMesh.generated.h"
#include "BsScriptPhysicsMaterial.generated.h"
#include "BsScriptAnimationClip.generated.h"
#include "BsScriptAudioClip.generated.h"
#include "BsScriptShader.generated.h"
#include "BsScriptMaterial.generated.h"
#include "BsScriptFont.generated.h"
#include "BsScriptSpriteTexture.generated.h"
#include "BsScriptStringTable.generated.h"
#include "Wrappers/BsScriptRRefBase.h"

namespace bs
{
	LOOKUP_BEGIN
		ADD_ENTRY(Texture, ScriptTexture, ScriptResourceType::Texture)
		ADD_ENTRY(SpriteTexture, ScriptSpriteTexture, ScriptResourceType::SpriteTexture)
		ADD_ENTRY(Mesh, ScriptMesh, ScriptResourceType::Mesh)
		ADD_ENTRY(Font, ScriptFont, ScriptResourceType::Font)
		ADD_ENTRY(Shader, ScriptShader, ScriptResourceType::Shader)
		ADD_ENTRY(ShaderInclude, ScriptShaderInclude, ScriptResourceType::ShaderInclude)
		ADD_ENTRY(Material, ScriptMaterial, ScriptResourceType::Material)
		ADD_ENTRY(Prefab, ScriptPrefab, ScriptResourceType::Prefab)
		ADD_ENTRY(PlainText, ScriptPlainText, ScriptResourceType::PlainText)
		ADD_ENTRY(ScriptCode, ScriptScriptCode, ScriptResourceType::ScriptCode)
		ADD_ENTRY(StringTable, ScriptStringTable, ScriptResourceType::StringTable)
		ADD_ENTRY(GUISkin, ScriptGUISkin, ScriptResourceType::GUISkin)
		ADD_ENTRY(PhysicsMaterial, ScriptPhysicsMaterial, ScriptResourceType::PhysicsMaterial)
		ADD_ENTRY(PhysicsMesh, ScriptPhysicsMesh, ScriptResourceType::PhysicsMesh)
		ADD_ENTRY(AudioClip, ScriptAudioClip, ScriptResourceType::AudioClip)
		ADD_ENTRY(AnimationClip, ScriptAnimationClip, ScriptResourceType::AnimationClip)
	LOOKUP_END

#undef LOOKUP_BEGIN
#undef ADD_ENTRY
#undef LOOKUP_END

	Vector<String> ScriptAssemblyManager::getScriptAssemblies() const
	{
		Vector<String> initializedAssemblies;
		for (auto& assemblyPair : mAssemblyInfos)
			initializedAssemblies.push_back(assemblyPair.first);

		return initializedAssemblies;
	}

	void ScriptAssemblyManager::loadAssemblyInfo(const String& assemblyName)
	{
		if(!mBaseTypesInitialized)
			initializeBaseTypes();

		initializeBuiltinComponentInfos();
		initializeBuiltinResourceInfos();

		// Process all classes and fields
		UINT32 mUniqueTypeId = 1;

		MonoAssembly* curAssembly = MonoManager::instance().getAssembly(assemblyName);
		if(curAssembly == nullptr)
			return;

		SPtr<ManagedSerializableAssemblyInfo> assemblyInfo = bs_shared_ptr_new<ManagedSerializableAssemblyInfo>();
		assemblyInfo->mName = assemblyName;

		mAssemblyInfos[assemblyName] = assemblyInfo;

		MonoClass* resourceClass = ScriptResource::getMetaData()->scriptClass;
		MonoClass* managedResourceClass = ScriptManagedResource::getMetaData()->scriptClass;

		// Populate class data
		const Vector<MonoClass*>& allClasses = curAssembly->getAllClasses();
		for(auto& curClass : allClasses)
		{
			if ((curClass->isSubClassOf(mBuiltin.componentClass) || curClass->isSubClassOf(resourceClass) ||
				curClass->hasAttribute(mBuiltin.serializeObjectAttribute)) && 
				curClass != mBuiltin.componentClass && curClass != resourceClass &&
				curClass != mBuiltin.managedComponentClass && curClass != managedResourceClass)
			{
				SPtr<ManagedSerializableTypeInfoObject> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoObject>();
				typeInfo->mTypeNamespace = curClass->getNamespace();
				typeInfo->mTypeName = curClass->getTypeName();
				typeInfo->mTypeId = mUniqueTypeId++;

				MonoPrimitiveType monoPrimitiveType = MonoUtil::getPrimitiveType(curClass->_getInternalClass());

				if(monoPrimitiveType == MonoPrimitiveType::ValueType)
					typeInfo->mValueType = true;
				else
					typeInfo->mValueType = false;

				SPtr<ManagedSerializableObjectInfo> objInfo = bs_shared_ptr_new<ManagedSerializableObjectInfo>();

				objInfo->mTypeInfo = typeInfo;
				objInfo->mMonoClass = curClass;

				assemblyInfo->mTypeNameToId[objInfo->getFullTypeName()] = typeInfo->mTypeId;
				assemblyInfo->mObjectInfos[typeInfo->mTypeId] = objInfo;
			}
		}

		// Populate field & property data
		for(auto& curClassInfo : assemblyInfo->mObjectInfos)
		{
			SPtr<ManagedSerializableObjectInfo> objInfo = curClassInfo.second;

			UINT32 mUniqueFieldId = 1;

			const Vector<MonoField*>& fields = objInfo->mMonoClass->getAllFields();
			for(auto& field : fields)
			{
				if(field->isStatic())
					continue;

				SPtr<ManagedSerializableTypeInfo> typeInfo = getTypeInfo(field->getType());
				if (typeInfo == nullptr)
					continue;

				SPtr<ManagedSerializableFieldInfo> fieldInfo = bs_shared_ptr_new<ManagedSerializableFieldInfo>();
				fieldInfo->mFieldId = mUniqueFieldId++;
				fieldInfo->mName = field->getName();
				fieldInfo->mMonoField = field;
				fieldInfo->mTypeInfo = typeInfo;
				fieldInfo->mParentTypeId = objInfo->mTypeInfo->mTypeId;
				
				MonoMemberVisibility visibility = field->getVisibility();
				if (visibility == MonoMemberVisibility::Public)
				{
					if (!field->hasAttribute(mBuiltin.dontSerializeFieldAttribute))
						fieldInfo->mFlags |= ScriptFieldFlag::Serializable;

					if (!field->hasAttribute(mBuiltin.hideInInspectorAttribute))
						fieldInfo->mFlags |= ScriptFieldFlag::Inspectable;

					fieldInfo->mFlags |= ScriptFieldFlag::Animable;
				}
				else
				{
					if (field->hasAttribute(mBuiltin.serializeFieldAttribute))
						fieldInfo->mFlags |= ScriptFieldFlag::Serializable;

					if (field->hasAttribute(mBuiltin.showInInspectorAttribute))
						fieldInfo->mFlags |= ScriptFieldFlag::Inspectable;
				}

				if (field->hasAttribute(mBuiltin.rangeAttribute))
					fieldInfo->mFlags |= ScriptFieldFlag::Range;

				if (field->hasAttribute(mBuiltin.stepAttribute))
					fieldInfo->mFlags |= ScriptFieldFlag::Step;

				objInfo->mFieldNameToId[fieldInfo->mName] = fieldInfo->mFieldId;
				objInfo->mFields[fieldInfo->mFieldId] = fieldInfo;
			}

			const Vector<MonoProperty*>& properties = objInfo->mMonoClass->getAllProperties();
			for (auto& property : properties)
			{
				SPtr<ManagedSerializableTypeInfo> typeInfo = getTypeInfo(property->getReturnType());
				if (typeInfo == nullptr)
					continue;

				SPtr<ManagedSerializablePropertyInfo> propertyInfo = bs_shared_ptr_new<ManagedSerializablePropertyInfo>();
				propertyInfo->mFieldId = mUniqueFieldId++;
				propertyInfo->mName = property->getName();
				propertyInfo->mMonoProperty = property;
				propertyInfo->mTypeInfo = typeInfo;
				propertyInfo->mParentTypeId = objInfo->mTypeInfo->mTypeId;

				if (!property->isIndexed())
				{
					MonoMemberVisibility visibility = property->getVisibility();
					if (visibility == MonoMemberVisibility::Public)
						propertyInfo->mFlags |= ScriptFieldFlag::Animable;

					if (property->hasAttribute(mBuiltin.serializeFieldAttribute))
						propertyInfo->mFlags |= ScriptFieldFlag::Serializable;

					if (property->hasAttribute(mBuiltin.showInInspectorAttribute))
						propertyInfo->mFlags |= ScriptFieldFlag::Inspectable;
				}

				if (property->hasAttribute(mBuiltin.rangeAttribute))
					propertyInfo->mFlags |= ScriptFieldFlag::Range;

				if (property->hasAttribute(mBuiltin.stepAttribute))
					propertyInfo->mFlags |= ScriptFieldFlag::Step;

				objInfo->mFieldNameToId[propertyInfo->mName] = propertyInfo->mFieldId;
				objInfo->mFields[propertyInfo->mFieldId] = propertyInfo;
			}
		}

		// Form parent/child connections
		for(auto& curClass : assemblyInfo->mObjectInfos)
		{
			MonoClass* base = curClass.second->mMonoClass->getBaseClass();
			while(base != nullptr)
			{
				SPtr<ManagedSerializableObjectInfo> baseObjInfo;
				if(getSerializableObjectInfo(base->getNamespace(), base->getTypeName(), baseObjInfo))
				{
					curClass.second->mBaseClass = baseObjInfo;
					baseObjInfo->mDerivedClasses.push_back(curClass.second);

					break;
				}

				base = base->getBaseClass();
			}
		}
	}

	void ScriptAssemblyManager::clearAssemblyInfo()
	{
		clearScriptObjects();
		mAssemblyInfos.clear();
	}

	SPtr<ManagedSerializableTypeInfo> ScriptAssemblyManager::getTypeInfo(MonoClass* monoClass)
	{
		if(!mBaseTypesInitialized)
			BS_EXCEPT(InvalidStateException, "Calling getTypeInfo without previously initializing base types.");

		MonoPrimitiveType monoPrimitiveType = MonoUtil::getPrimitiveType(monoClass->_getInternalClass());
		
		// If enum get the enum base data type
		bool isEnum = MonoUtil::isEnum(monoClass->_getInternalClass());
		if (isEnum)
			monoPrimitiveType = MonoUtil::getEnumPrimitiveType(monoClass->_getInternalClass());

		//  Determine field type
		switch(monoPrimitiveType)
		{
		case MonoPrimitiveType::Boolean:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::Bool;
				return typeInfo;
			}
		case MonoPrimitiveType::Char:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::Char;
				return typeInfo;
			}
		case MonoPrimitiveType::I8:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::I8;
				return typeInfo;
			}
		case MonoPrimitiveType::U8:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::U8;
				return typeInfo;
			}
		case MonoPrimitiveType::I16:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::I16;
				return typeInfo;
			}
		case MonoPrimitiveType::U16:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::U16;
				return typeInfo;
			}
		case MonoPrimitiveType::I32:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::I32;
				return typeInfo;
			}
		case MonoPrimitiveType::U32:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::U32;
				return typeInfo;
			}
		case MonoPrimitiveType::I64:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::I64;
				return typeInfo;
			}
		case MonoPrimitiveType::U64:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::U64;
				return typeInfo;
			}
		case MonoPrimitiveType::String:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::String;
				return typeInfo;
			}
		case MonoPrimitiveType::R32:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::Float;
				return typeInfo;
			}
		case MonoPrimitiveType::R64:
			{
				SPtr<ManagedSerializableTypeInfoPrimitive> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoPrimitive>();
				typeInfo->mType = ScriptPrimitiveType::Double;
				return typeInfo;
			}
		case MonoPrimitiveType::Class:
			if(monoClass->isSubClassOf(ScriptResource::getMetaData()->scriptClass)) // Resource
			{
				SPtr<ManagedSerializableTypeInfoRef> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoRef>();
				typeInfo->mTypeNamespace = monoClass->getNamespace();
				typeInfo->mTypeName = monoClass->getTypeName();
				typeInfo->mRTIITypeId = 0;

				if(monoClass == ScriptResource::getMetaData()->scriptClass)
					typeInfo->mType = ScriptReferenceType::BuiltinResourceBase;
				else if (monoClass == ScriptManagedResource::getMetaData()->scriptClass)
					typeInfo->mType = ScriptReferenceType::ManagedResourceBase;
				else if (monoClass->isSubClassOf(ScriptManagedResource::getMetaData()->scriptClass))
					typeInfo->mType = ScriptReferenceType::ManagedResource;
				else if (monoClass->isSubClassOf(ScriptResource::getMetaData()->scriptClass))
				{
					typeInfo->mType = ScriptReferenceType::BuiltinResource;

					::MonoReflectionType* type = MonoUtil::getType(monoClass->_getInternalClass());
					BuiltinResourceInfo* builtinInfo = getBuiltinResourceInfo(type);
					if (builtinInfo == nullptr)
					{
						assert(false && "Unable to find information about a built-in resource. Did you update BuiltinResourceTypes list?");
						return nullptr;
					}

					typeInfo->mRTIITypeId = builtinInfo->typeId;
				}

				return typeInfo;
			}
			else if(monoClass == ScriptRRefBase::getMetaData()->scriptClass) // Resource reference
				return bs_shared_ptr_new<ManagedSerializableTypeInfoRRef>();
			else if (monoClass->isSubClassOf(mBuiltin.sceneObjectClass) || monoClass->isSubClassOf(mBuiltin.componentClass)) // Game object
			{
				SPtr<ManagedSerializableTypeInfoRef> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoRef>();
				typeInfo->mTypeNamespace = monoClass->getNamespace();
				typeInfo->mTypeName = monoClass->getTypeName();
				typeInfo->mRTIITypeId = 0;

				if (monoClass == mBuiltin.componentClass)
					typeInfo->mType = ScriptReferenceType::BuiltinComponentBase;
				else if (monoClass == mBuiltin.managedComponentClass)
					typeInfo->mType = ScriptReferenceType::ManagedComponentBase;
				else if (monoClass->isSubClassOf(mBuiltin.sceneObjectClass))
					typeInfo->mType = ScriptReferenceType::SceneObject;
				else if (monoClass->isSubClassOf(mBuiltin.managedComponentClass))
					typeInfo->mType = ScriptReferenceType::ManagedComponent;
				else if (monoClass->isSubClassOf(mBuiltin.componentClass))
				{
					typeInfo->mType = ScriptReferenceType::BuiltinComponent;

					::MonoReflectionType* type = MonoUtil::getType(monoClass->_getInternalClass());
					BuiltinComponentInfo* builtinInfo = getBuiltinComponentInfo(type);
					if(builtinInfo == nullptr)
					{
						assert(false && "Unable to find information about a built-in component. Did you update BuiltinComponents list?");
						return nullptr;
					}

					typeInfo->mRTIITypeId = builtinInfo->typeId;
				}

				return typeInfo;
			}
			else
			{
				SPtr<ManagedSerializableObjectInfo> objInfo;
				if (getSerializableObjectInfo(monoClass->getNamespace(), monoClass->getTypeName(), objInfo))
					return objInfo->mTypeInfo;
			}

			break;
		case MonoPrimitiveType::ValueType:
			{
				SPtr<ManagedSerializableObjectInfo> objInfo;
				if (getSerializableObjectInfo(monoClass->getNamespace(), monoClass->getTypeName(), objInfo))
					return objInfo->mTypeInfo;
			}

			break;
		case MonoPrimitiveType::Generic:
			if(monoClass->getFullName() == mBuiltin.systemGenericListClass->getFullName()) // Full name is part of CIL spec, so it is just fine to compare like this
			{
				SPtr<ManagedSerializableTypeInfoList> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoList>();

				MonoProperty* itemProperty = monoClass->getProperty("Item");
				MonoClass* itemClass = itemProperty->getReturnType();

				if (itemClass != nullptr)
					typeInfo->mElementType = getTypeInfo(itemClass);
				
				if (typeInfo->mElementType == nullptr)
					return nullptr;

				return typeInfo;
			}
			else if(monoClass->getFullName() == mBuiltin.systemGenericDictionaryClass->getFullName())
			{
				SPtr<ManagedSerializableTypeInfoDictionary> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoDictionary>();

				MonoMethod* getEnumerator = monoClass->getMethod("GetEnumerator");
				MonoClass* enumClass = getEnumerator->getReturnType();

				MonoProperty* currentProp = enumClass->getProperty("Current");
				MonoClass* keyValuePair = currentProp->getReturnType();

				MonoProperty* keyProperty = keyValuePair->getProperty("Key");
				MonoProperty* valueProperty = keyValuePair->getProperty("Value");

				MonoClass* keyClass = keyProperty->getReturnType();
				if(keyClass != nullptr)
					typeInfo->mKeyType = getTypeInfo(keyClass);

				MonoClass* valueClass = valueProperty->getReturnType();
				if(valueClass != nullptr)
					typeInfo->mValueType = getTypeInfo(valueClass);

				if (typeInfo->mKeyType == nullptr || typeInfo->mValueType == nullptr)
					return nullptr;

				return typeInfo;
			}
			else if(monoClass->getFullName() == mBuiltin.genericRRefClass->getFullName())
			{
				SPtr<ManagedSerializableTypeInfoRRef> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoRRef>();
				
				MonoProperty* itemProperty = monoClass->getProperty("Value");
				MonoClass* itemClass = itemProperty->getReturnType();

				if (itemClass != nullptr)
					typeInfo->mResourceType = getTypeInfo(itemClass);
				
				if (typeInfo->mResourceType == nullptr)
					return nullptr;

				return typeInfo;
			}
			break;
		case MonoPrimitiveType::Array:
			{
				SPtr<ManagedSerializableTypeInfoArray> typeInfo = bs_shared_ptr_new<ManagedSerializableTypeInfoArray>();

				::MonoClass* elementClass = ScriptArray::getElementClass(monoClass->_getInternalClass());
				if(elementClass != nullptr)
				{
					MonoClass* monoElementClass = MonoManager::instance().findClass(elementClass);
					if(monoElementClass != nullptr)
						typeInfo->mElementType = getTypeInfo(monoElementClass);
				}

				if (typeInfo->mElementType == nullptr)
					return nullptr;

				typeInfo->mRank = ScriptArray::getRank(monoClass->_getInternalClass());

				return typeInfo;
			}
		default:
			break;
		}

		return nullptr;
	}

	void ScriptAssemblyManager::clearScriptObjects()
	{
		mBaseTypesInitialized = false;
		mBuiltin = BuiltinScriptClasses();
	}

	void ScriptAssemblyManager::initializeBaseTypes()
	{
		// Get necessary classes for detecting needed class & field information
		MonoAssembly* corlib = MonoManager::instance().getAssembly("corlib");
		if(corlib == nullptr)
			BS_EXCEPT(InvalidStateException, "corlib assembly is not loaded.");

		MonoAssembly* bansheeEngineAssembly = MonoManager::instance().getAssembly(ENGINE_ASSEMBLY);
		if(bansheeEngineAssembly == nullptr)
			BS_EXCEPT(InvalidStateException, String(ENGINE_ASSEMBLY) +  " assembly is not loaded.");

		mBuiltin.systemArrayClass = corlib->getClass("System", "Array");
		if(mBuiltin.systemArrayClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find System.Array managed class.");

		mBuiltin.systemGenericListClass = corlib->getClass("System.Collections.Generic", "List`1");
		if(mBuiltin.systemGenericListClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find List<T> managed class.");

		mBuiltin.systemGenericDictionaryClass = corlib->getClass("System.Collections.Generic", "Dictionary`2");
		if(mBuiltin.systemGenericDictionaryClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find Dictionary<TKey, TValue> managed class.");

		mBuiltin.systemTypeClass = corlib->getClass("System", "Type");
		if (mBuiltin.systemTypeClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find Type managed class.");

		mBuiltin.serializeObjectAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "SerializeObject");
		if(mBuiltin.serializeObjectAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find SerializableObject managed class.");

		mBuiltin.dontSerializeFieldAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "DontSerializeField");
		if(mBuiltin.dontSerializeFieldAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find DontSerializeField managed class.");

		mBuiltin.rangeAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "Range");
		if (mBuiltin.rangeAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find Range managed class.");

		mBuiltin.stepAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "Step");
		if (mBuiltin.stepAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find Step managed class.");

		mBuiltin.componentClass = bansheeEngineAssembly->getClass("BansheeEngine", "Component");
		if(mBuiltin.componentClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find Component managed class.");

		mBuiltin.managedComponentClass = bansheeEngineAssembly->getClass("BansheeEngine", "ManagedComponent");
		if (mBuiltin.managedComponentClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find ManagedComponent managed class.");

		mBuiltin.missingComponentClass = bansheeEngineAssembly->getClass("BansheeEngine", "MissingComponent");
		if (mBuiltin.missingComponentClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find MissingComponent managed class.");

		mBuiltin.sceneObjectClass = bansheeEngineAssembly->getClass("BansheeEngine", "SceneObject");
		if(mBuiltin.sceneObjectClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find SceneObject managed class.");

		mBuiltin.rrefBaseClass = bansheeEngineAssembly->getClass("BansheeEngine", "RRefBase");
		if(mBuiltin.rrefBaseClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find RRefBase managed class.");

		mBuiltin.genericRRefClass = bansheeEngineAssembly->getClass("BansheeEngine", "RRef`1");
		if(mBuiltin.genericRRefClass == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find RRef<T> managed class.");

		mBuiltin.serializeFieldAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "SerializeField");
		if(mBuiltin.serializeFieldAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find SerializeField managed class.");

		mBuiltin.hideInInspectorAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "HideInInspector");
		if(mBuiltin.hideInInspectorAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find HideInInspector managed class.");

		mBuiltin.showInInspectorAttribute = bansheeEngineAssembly->getClass("BansheeEngine", "ShowInInspector");
		if (mBuiltin.showInInspectorAttribute == nullptr)
			BS_EXCEPT(InvalidStateException, "Cannot find ShowInInspector managed class.");

		mBaseTypesInitialized = true;
	}

	void ScriptAssemblyManager::initializeBuiltinComponentInfos()
	{
		mBuiltinComponentInfos.clear();
		mBuiltinComponentInfosByTID.clear();

		Vector<BuiltinComponentInfo> allComponentsInfos = BuiltinComponents::getEntries();

		for(auto& entry : allComponentsInfos)
		{
			MonoAssembly* assembly = MonoManager::instance().getAssembly(entry.metaData->assembly);
			if (assembly == nullptr)
				continue;

			BuiltinComponentInfo info = entry;
			info.monoClass = assembly->getClass(entry.metaData->ns, entry.metaData->name);

			::MonoReflectionType* type = MonoUtil::getType(info.monoClass->_getInternalClass());

			mBuiltinComponentInfos[type] = info;
			mBuiltinComponentInfosByTID[info.typeId] = info;
		}
	}

	BuiltinComponentInfo* ScriptAssemblyManager::getBuiltinComponentInfo(::MonoReflectionType* type)
	{
		auto iterFind = mBuiltinComponentInfos.find(type);
		if (iterFind == mBuiltinComponentInfos.end())
			return nullptr;

		return &(iterFind->second);
	}

	BuiltinComponentInfo* ScriptAssemblyManager::getBuiltinComponentInfo(UINT32 rttiTypeId)
	{
		auto iterFind = mBuiltinComponentInfosByTID.find(rttiTypeId);
		if (iterFind == mBuiltinComponentInfosByTID.end())
			return nullptr;

		return &(iterFind->second);
	}

	void ScriptAssemblyManager::initializeBuiltinResourceInfos()
	{
		mBuiltinResourceInfos.clear();
		mBuiltinResourceInfosByTID.clear();
		mBuiltinResourceInfosByType.clear();

		Vector<BuiltinResourceInfo> allResourceInfos = BuiltinResourceTypes::getEntries();

		for (auto& entry : allResourceInfos)
		{
			MonoAssembly* assembly = MonoManager::instance().getAssembly(entry.metaData->assembly);
			if (assembly == nullptr)
				continue;

			BuiltinResourceInfo info = entry;
			info.monoClass = assembly->getClass(entry.metaData->ns, entry.metaData->name);

			::MonoReflectionType* type = MonoUtil::getType(info.monoClass->_getInternalClass());

			mBuiltinResourceInfos[type] = info;
			mBuiltinResourceInfosByTID[info.typeId] = info;
			mBuiltinResourceInfosByType[(UINT32)info.resType] = info;
		}
	}

	BuiltinResourceInfo* ScriptAssemblyManager::getBuiltinResourceInfo(::MonoReflectionType* type)
	{
		auto iterFind = mBuiltinResourceInfos.find(type);
		if (iterFind == mBuiltinResourceInfos.end())
			return nullptr;

		return &(iterFind->second);
	}

	BuiltinResourceInfo* ScriptAssemblyManager::getBuiltinResourceInfo(UINT32 rttiTypeId)
	{
		auto iterFind = mBuiltinResourceInfosByTID.find(rttiTypeId);
		if (iterFind == mBuiltinResourceInfosByTID.end())
			return nullptr;

		return &(iterFind->second);
	}

	BuiltinResourceInfo* ScriptAssemblyManager::getBuiltinResourceInfo(ScriptResourceType type)
	{
		auto iterFind = mBuiltinResourceInfosByType.find((UINT32)type);
		if (iterFind == mBuiltinResourceInfosByType.end())
			return nullptr;

		return &(iterFind->second);
	}

	bool ScriptAssemblyManager::getSerializableObjectInfo(const String& ns, const String& typeName, SPtr<ManagedSerializableObjectInfo>& outInfo)
	{
		String fullName = ns + "." + typeName;
		for(auto& curAssembly : mAssemblyInfos)
		{
			if (curAssembly.second == nullptr)
				continue;

			auto iterFind = curAssembly.second->mTypeNameToId.find(fullName);
			if(iterFind != curAssembly.second->mTypeNameToId.end())
			{
				outInfo = curAssembly.second->mObjectInfos[iterFind->second];

				return true;
			}
		}

		return false;
	}

	bool ScriptAssemblyManager::hasSerializableObjectInfo(const String& ns, const String& typeName)
	{
		String fullName = ns + "." + typeName;
		for(auto& curAssembly : mAssemblyInfos)
		{
			auto iterFind = curAssembly.second->mTypeNameToId.find(fullName);
			if(iterFind != curAssembly.second->mTypeNameToId.end())
				return true;
		}

		return false;
	}
}
