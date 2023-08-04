#pragma once
#include "../JsClass.h"
#include "client/feature/module/script/JsModule.h"

class JsModuleClass : public JsClass {
protected:
	static JsValueRef CALLBACK jsConstructor(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState) {
		auto thi = reinterpret_cast<JsModuleClass*>(callbackState);

		if (!Chakra::VerifyArgCount(argCount, 5)) return JS_INVALID_REFERENCE;
		if (!Chakra::VerifyParameters({ {arguments[1], JsString}, {arguments[2], JsString}, {arguments[3], JsString}, {arguments[4], JsNumber} })) return JS_INVALID_REFERENCE;

		auto jsm = new JsModule(util::WStrToStr(Chakra::GetString(arguments[1])), util::WStrToStr(Chakra::GetString(arguments[2])), util::WStrToStr(Chakra::GetString(arguments[3])), Chakra::GetInt(arguments[4]));
		return thi->construct(jsm, true);
	}

	static JsValueRef CALLBACK toStringCallback(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState) {
		auto thi = reinterpret_cast<JsModuleClass*>(callbackState);
		auto mod = ToModule(arguments[0]);
		if (!mod) {
			Chakra::ThrowError(L"Invalid module");
			return JS_INVALID_REFERENCE;
		}
		std::string add = std::format("{} ({})", util::WStrToStr(thi->name), mod->name());
		return Chakra::MakeString(L"[object " + util::StrToWStr(add) + L"]");
	}

	static JsValueRef CALLBACK moduleIsEnabled(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState);
	static JsValueRef CALLBACK moduleSetEnabled(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState);
	static JsValueRef CALLBACK moduleIsBlocked(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState);
	static JsValueRef CALLBACK moduleSetOnEvent(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState);
public:
	JsModuleClass() : JsClass(L"Module") {
		createConstructor(jsConstructor, this);
	}

	JsValueRef construct(JsModule* mod, bool finalize) {
		JsValueRef obj;
		if (finalize) {
			JS::JsCreateExternalObject(mod, [](void* obj) {
				delete obj;
				}, &obj);
		}
		else {
			JS::JsCreateExternalObject(mod, [](void*) {
				}, &obj);
		}

		JS::JsSetPrototype(obj, getPrototype());

		Chakra::SetPropertyNumber(obj, L"key", static_cast<double>(mod->getKeybind()));
		Chakra::SetPropertyString(obj, L"name", util::StrToWStr(mod->name()));
		Chakra::SetPropertyString(obj, L"displayName", util::StrToWStr(mod->getDisplayName()));
		Chakra::SetPropertyString(obj, L"description", util::StrToWStr(mod->desc()));
		Chakra::SetPropertyBool(obj, L"visual", mod->isHud());
		Chakra::SetPropertyBool(obj, L"visible", mod->isVisible());

		return obj;
	}

	void prepareFunctions() override {
		Chakra::DefineFunc(prototype, toStringCallback, L"toString", this);
		Chakra::DefineFunc(prototype, moduleIsEnabled, L"isEnabled", this);
		Chakra::DefineFunc(prototype, moduleSetEnabled, L"setEnabled", this);
		Chakra::DefineFunc(prototype, moduleIsBlocked, L"isBlocked", this);
		Chakra::DefineFunc(prototype, moduleSetOnEvent, L"on", this);

	};

	static JsModule* ToModule(JsValueRef obj) {
		JsModule* mod = nullptr;
		JS::JsGetExternalData(obj, reinterpret_cast<void**>(&mod));
		return mod;
	}
};