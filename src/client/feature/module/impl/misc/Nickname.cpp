#include "pch.h"
#include "Nickname.h"

Nickname::Nickname() : Module("Nickname", "Nickname", "Sets your nickname in the chat.", GAME) {
	addSetting("nick", "Nickname", "Your new nickname.", this->nickname);

    listen<ClientTextEvent>((EventListenerFunc)&Nickname::onClientTextPacket);
}

void Nickname::onClientTextPacket(Event& evG) {
    auto textPacket = reinterpret_cast<ClientTextEvent&>(evG).getTextPacket();

    if (!SDK::ClientInstance::get()->getLocalPlayer()) return;

    auto message = textPacket->str.str();
    auto source = textPacket->source.str();

    auto replaceAll = [](std::string& s, std::string from, std::string to) {
        if (!from.empty())
            for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
                s.replace(pos, from.size(), to);
        return s;
    };

    std::string newName = util::WStrToStr(std::get<TextValue>(this->nickname).str);
    auto& currentPlayerName = SDK::ClientInstance::get()->getLocalPlayer()->playerName;
    replaceAll(message, currentPlayerName, newName);
    replaceAll(source, currentPlayerName, newName);

    textPacket->str.setString(message.c_str());
    textPacket->source.setString(source.c_str());
}