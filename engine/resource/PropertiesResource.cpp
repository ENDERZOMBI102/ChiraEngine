#include "PropertiesResource.h"

#include <core/Logger.h>
#include <i18n/TranslationManager.h>

using namespace chira;

CHIRA_CREATE_LOG(PROPERTIESRESOURCE);

void PropertiesResource::compile(const byte buffer[], std::size_t bufferLength) {
    nlohmann::json props;
    try {
        props = nlohmann::json::parse(std::string{reinterpret_cast<const char*>(buffer), bufferLength});
    } catch (const nlohmann::json::exception&) {
        LOG_PROPERTIESRESOURCE.error(TRF("error.properties_resource.invalid_json", this->identifier));
    }
    this->compile(props);
}
