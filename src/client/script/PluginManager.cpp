#include "pch.h"
// brace yourselves
#include "PluginManager.h"
#include "client/Latite.h"
#include "client/misc/ClientMessageSink.h"
#include "client/event/Eventing.h"
#include "client/event/impl/UpdateEvent.h"

#include "sdk/common/client/game/ClientInstance.h"
#include "sdk/common/client/player/LocalPlayer.h"

#include "util/Logger.h"
#include <memory>


#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include "util/XorString.h"


using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Filters;

PluginManager::PluginManager() {
}

std::filesystem::path PluginManager::getUserDir() {
	return util::GetLatitePath() / "Plugins";
}

std::filesystem::path PluginManager::getUserPrerunDir() {
	return getUserDir() / "Startup";
}

std::shared_ptr<JsPlugin> PluginManager::loadPlugin(std::wstring const& folderPath, bool run)
{
	auto& fPathW = folderPath;
	auto scriptPath = getUserDir() / fPathW;
	if (!std::filesystem::exists(scriptPath)) return nullptr;

	for (auto& scr : this->items) {
		if (scr->getRelFolderPath() == fPathW) {
			Latite::getClientMessageSink().push(util::Format(std::format("Plugin {} is already loaded.", util::WStrToStr(scr->getName()))));
			return nullptr;
		}
	}

	auto myScript = std::make_shared<JsPlugin>(scriptPath);
	if (!myScript->load()) return nullptr;
	this->items.push_back(myScript);

	if (run) {
		Event::Value val{L"scriptName"};
		val.val= myScript->getName();
		Event::Value val2{L"scriptVersion"};
		val2.val = myScript->getVersion();
		Event::Value val3{L"scriptAuthor"};
		val3.val = myScript->getAuthor();
		Event newEv{ L"load-script", {val, val2, val3}, false };
		dispatchEvent(newEv);
	}
	return myScript;
}

std::shared_ptr<JsPlugin> PluginManager::getPluginByName(std::wstring const& name)
{
	for (auto& script : items) {
		if (script->getFolderName() == name) {
			return script;
		}
	}
	return nullptr;
}

void PluginManager::popScript(std::shared_ptr<JsPlugin> ptr)
{
	for (auto it = items.begin(); it != items.end(); it++) {
		if (*it == ptr) {
			unloadScript(*it);
			items.erase(it);
			return;
		}
	}
}

void PluginManager::reportError(JsValueRef except, std::wstring filePath) {
	auto str = util::WStrToStr(Chakra::ToString(except));

	std::stringstream ss;
	ss << "&c" << util::WStrToStr(filePath) << ": " << str;

	Latite::getClientMessageSink().display(util::Format(ss.str()));
	// not sure if you release the exception or not, will do it anyway
	Chakra::Release(except);
}

void PluginManager::handleErrors(JsErrorCode code) {
	JsContextRef ctx;
	JS::JsGetCurrentContext(&ctx);
	JsScript* script = JsScript::getThis();
	if (script) {
		if (code == JsErrorScriptException) {
			JsValueRef except;
			JS::JsGetAndClearException(&except);
			reportError(except, script->data.name);
		}
		else if (code != JsNoError) {
			Latite::getClientMessageSink().display(util::Format(std::format("&cA JS error occured in script {}: JsErrorCode {}", util::WStrToStr(script->data.name), (int)code)));
		}
	}
}

bool PluginManager::loadPrerunScripts()
{
	if (!scriptingSupported()) {
		Logger::Warn("Scripting is not supported");
		return false;
	}

	auto prerunPath = getUserPrerunDir();
	std::filesystem::create_directory(prerunPath);
	using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
	for (auto& dirEntry : recursive_directory_iterator(prerunPath)) {
		if (dirEntry.is_directory()) {
			if (std::filesystem::exists(dirEntry.path().string() + "\\index.js")) {
				loadPlugin(getUserPrerunDir().filename().wstring() + L"\\" + dirEntry.path().filename().wstring(), true);
			}
		}
	}
	return true;
}

void PluginManager::runScriptingOperations()
{
	if (!scriptingSupported()) return;

	for (auto& plug : this->items) {
		plug->handleAsyncOperations();

		for (auto& scr : plug->getScripts()) {
			// timeouts
			for (auto it = scr->timeouts.begin(); it != scr->timeouts.end();) {
				auto now = std::chrono::system_clock::now();
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->createTime);
				if (duration.count() > it->time) {
					JS::JsSetCurrentContext(it->context);
					JsValueRef res;
					handleErrors(Chakra::CallFunction(it->callback, &it->callback, 1, &res));
					JS::JsRelease(res, nullptr);
					scr->timeouts.erase(it);
					continue;
				}
				++it;
			}

			// intervals
			for (auto& tim : scr->intervals) {
				auto now = std::chrono::system_clock::now();
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - tim.createTime);
				if (duration.count() > tim.time) {
					JS::JsSetCurrentContext(tim.context);
					JsValueRef res;
					handleErrors(Chakra::CallFunction(tim.callback, &tim.callback, 1, &res));
					JS::JsRelease(res, nullptr);
					tim.createTime = now;
				}
			}
		}
	}

	// drawing
	Event ev{ L"renderDX", {}, false };
	dispatchEvent(ev);
}

std::optional<int> PluginManager::installScript(std::string const& inName) {
	std::wstring registry = L"https://raw.githubusercontent.com/LatiteScripting/Scripts/master/Scripts";
	std::wstring jsonPath = registry + L"/scripts.json";
	nlohmann::json scriptsJson;

	auto message = [](std::string const& msg, bool err = false) -> void {
		if (err) {
			Latite::getClientMessageSink().push(util::Format("[Script Installer] &c" + msg));
		}
		else Latite::getClientMessageSink().push("[Script Installer] " + msg);
	};

	auto http = HttpClient();
	{
		// get JSON
		winrt::Windows::Foundation::Uri requestUri(jsonPath);

		HttpRequestMessage request(HttpMethod::Get(), requestUri);

		try {
			auto operation = http.SendRequestAsync(request);
			auto response = operation.get();
			auto cont = response.Content();
			auto strs = cont.ReadAsStringAsync().get();

			try {
				scriptsJson = nlohmann::json::parse(std::wstring(strs.c_str()));
			}
			catch (nlohmann::json::parse_error& e) {
				Latite::getClientMessageSink().push(std::string("JSON error while installing script: ") + e.what());
				return 0;
			}
		}
		catch (winrt::hresult_error const& err) {
			Latite::getClientMessageSink().push(util::WStrToStr(err.message().c_str()));
			return 0;
		}
	}
	auto& arr = scriptsJson["Plugins"];
	for (auto& js : arr) {
		auto name = js["name"].get<std::string>();
		auto oName = js["name"].get<std::string>();
		auto woName = util::StrToWStr(oName);
		std::string in = inName;
		std::transform(in.begin(), in.end(), in.begin(), ::tolower);
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		if (in == name) {
			message("Installing " + oName + " v" + js["version"].get<std::string>() + " by " + js["author"].get<std::string>());
			std::filesystem::path path = getUserPrerunDir() / woName;
			std::filesystem::create_directories(path);
			for (auto& fil : js["files"]) {
				auto fws = util::StrToWStr(fil.get<std::string>());
				winrt::Windows::Foundation::Uri requestUri(registry + L"/" + woName + L"/" + fws);
				HttpRequestMessage request(HttpMethod::Get(), requestUri);
				auto operation = http.SendRequestAsync(request);
				auto response = operation.get();
				auto cont = response.Content();
				auto strs = cont.ReadAsStringAsync().get();
				std::wofstream ofs;
				ofs.open(path / fws);
				if (ofs.fail()) {
					message("Error opening file: " + std::to_string(*_errno()), true);
					return *_errno();
				}
				ofs << strs.c_str();
				ofs.close();
			}
			// generate certificate
			auto cert = JsPlugin::getHash(path);
			if (cert) {
#if LATITE_DEBUG
				Logger::Info("Generated certificate {}", util::WStrToStr(cert.value()));
#endif
				std::wofstream ofs(path / XOR_STRING("certificate"));
				ofs << cert.value();
				ofs.flush(); // so we can access the certificate directly after
			}
			else {
				Logger::Warn(XOR_STRING("Could not create certificate for script {}"), path.string());
			}
			return std::nullopt;
		}
	}
	message("Could not find script " + inName, true);
	return 0;
}

void PluginManager::init()
{
	auto scriptsPath = getUserDir();
	std::filesystem::create_directory(scriptsPath);

	initListeners();
	int id = 0;
	//this->objects.push_back(std::make_shared<ClientScriptingObject>(id++));
	//this->objects.push_back(std::make_shared<GameScriptingObject>(id++));
	//this->objects.push_back(std::make_shared<GraphicsScriptingObject>(id++));
	//this->objects.push_back(std::make_shared<Graphics3DScriptingObject>(id++));
}

void PluginManager::initListeners()
{
	eventListeners[L"world-tick"] = {};
	eventListeners[L"join-game"] = {};
	eventListeners[L"leave-game"] = {};
	eventListeners[L"receive-chat"] = {};
	eventListeners[L"title"] = {};
	eventListeners[L"send-chat"] = {};
	eventListeners[L"render2d"] = {};
	eventListeners[L"renderDX"] = {};
	eventListeners[L"attack"] = {};
	eventListeners[L"key-press"] = {};
	eventListeners[L"click"] = {};
	eventListeners[L"render3d"] = {};
	eventListeners[L"app-suspended"] = {};
	eventListeners[L"load-script"] = {};
	eventListeners[L"unload-script"] = {};
}

void PluginManager::unloadScript(std::shared_ptr<JsPlugin> ptr)
{
	Event::Value val{L"scriptName"};
	val.name = L"scriptName";
	val.val = ptr->getName();
	Event newEv{ L"unload-script", {val}, false };
	dispatchEvent(newEv);

	for (auto& ev : this->eventListeners) {
		if (ev.second.size() > 0)
			for (auto it = ev.second.begin(); it != ev.second.end();) {
				for (auto& scr : ptr->getScripts()) {
					if (it->second == scr->getContext()) {
						JS::JsSetCurrentContext(it->second);
						unsigned int refCount;
						JS::JsRelease(it->first, &refCount);
						ev.second.erase(it);
					}
					else {
						++it;
					}
				}
			}
	}
}

void PluginManager::unloadAll() {
	for (auto& s : this->items) {
		popScript(s);
	}
}

bool PluginManager::hasPermission(JsPlugin* script, Permission perm) {
	auto player = SDK::ClientInstance::get()->getLocalPlayer();
	if (!player) {
#if LATITE_DEBUG
		Logger::Warn("[Script] attempt to call hasPermission while player does not exist!");
#endif
		return false;
	}
	switch (perm) {
	case Permission::SendChat:
		return script->isTrusted() || player->getCommandPermissionLevel() > 0;
	case Permission::Operator:
		return player->getCommandPermissionLevel() > 0;
	}
	return false;
}

bool PluginManager::scriptingSupported() {
	return JS::JsAddRef;
}

void PluginManager::uninitialize() {
	for (auto& script : this->items) {
		popScript(script);
	}
}

bool PluginManager::dispatchEvent(Event& ev)
{
	for (auto& lis : eventListeners) {
		if (lis.first == ev.type) {
			for (auto& l : lis.second) {
				JS::JsSetCurrentContext(l.second);
				JsValueRef params[2] = {};
				// create the obj
				JS::JsGetUndefinedValue(params);
				JS::JsCreateObject(&params[1]);
				JS::JsAddRef(params[1], nullptr);

				if (ev.isCancellable) {

					Event::Value val{};
					val.val = false;
					val.name = L"cancel";
					ev.values.push_back(val);
				}

				for (auto& val : ev.values) {
					JsPropertyIdRef idRef;

					JsValueRef ref = JS_INVALID_REFERENCE;
					switch (val.val.index()) {
					case Event::Value::Bool:
					{
						JS::JsBoolToBoolean(std::get<bool>(val.val), &ref);
					}
					break;
					case Event::Value::Number:
					{
						JS::JsDoubleToNumber(std::get<double>(val.val), &ref);
					}
					break;
					case Event::Value::String:
					{
						JS::JsPointerToString(std::get<std::wstring>(val.val).c_str(), std::get<std::wstring>(val.val).size(), &ref);
					}
					break;
					case Event::Value::EntityRef:

					{
						// TODO: Entity refs
					}
					break;
					default:
						throw std::runtime_error("unknown value");
						break;
					}
					JS::JsGetPropertyIdFromName(val.name.c_str(), &idRef);
					JS::JsSetProperty(params[1], idRef, ref, true);
					Chakra::Release(ref);
				}
				JsValueRef ret;

				//int refc1 = Chakra::GetRefCount(l.first);
				//int refc = Chakra::GetRefCount(params[1]);
				handleErrors(Chakra::CallFunction(l.first, params, 2, &ret));

				if (ev.isCancellable) {
					auto b = Chakra::GetBoolProperty(params[1], L"cancel");
					if (b) return true;
				}

				Chakra::Release(ret);
				Chakra::Release(params[1]);
				//Chakra::Release(params[0]);
			}
			break;
		}
	}
	return false;
}