#pragma once
#include "../JsClass.h"
#include "util/LMath.h"
#include "util/ChakraUtil.h"

class JsVec2 : public JsClass {
protected:
	static JsValueRef CALLBACK jsConstructor(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState) {
		auto thi = reinterpret_cast<JsVec2*>(callbackState);

		if (argCount == 1) {
			return thi->construct(Vec2(0.f, 0.f));
		}

		if (!Chakra::VerifyArgCount(argCount, 3)) return JS_INVALID_REFERENCE;
		if (!Chakra::VerifyParameters({ {arguments[1], JsNumber}, {arguments[2], JsNumber } })) return JS_INVALID_REFERENCE;

		return thi->construct(Vec2((float)Chakra::GetNumber(arguments[1]), (float)Chakra::GetNumber(arguments[2])));
	}

	static JsValueRef CALLBACK toStringCallback(JsValueRef callee, bool isConstructor,
		JsValueRef* arguments, unsigned short argCount, void* callbackState) {
		auto thi = reinterpret_cast<JsVec2*>(callbackState);
		auto vec2 = ToVec2(arguments[0]);
		std::string add = std::format("{} (x={:.2}, y={:.2})", util::WStrToStr(thi->name), vec2.x, vec2.y);
		return Chakra::MakeString(L"[object " + util::StrToWStr(add) + L"]");
	}
public:
	JsVec2() : JsClass(L"Vector2") {
		createConstructor(jsConstructor, this);
	}

	JsValueRef construct(Vec2 const& vec) {
		JsValueRef obj;
		JS::JsCreateObject(&obj);
		JS::JsSetPrototype(obj, getPrototype());
		Chakra::SetPropertyNumber(obj, L"x", static_cast<double>(vec.x), true);
		Chakra::SetPropertyNumber(obj, L"y", static_cast<double>(vec.y), true);
		return obj;
	}

	void prepareFunctions() override {
		Chakra::SetPropertyNumber(prototype, L"x", 0.0, true);
		Chakra::SetPropertyNumber(prototype, L"y", 0.0, true);

		Chakra::DefineFunc(prototype, toStringCallback, L"toString", this);

	};

	static Vec2 ToVec2(JsValueRef obj) {
		return {
			static_cast<float>(Chakra::GetNumberProperty(obj, L"x")),
			static_cast<float>(Chakra::GetNumberProperty(obj, L"y"))
		};
	}
};