#include "StateFactory.h"
#include "Applications/MenuState.h"
#include "Applications/RelayState.h"
#include "Utils/Device.h"

namespace NuggetsInc {

AppState* StateFactory::createState(StateType type) {
    // Create the appropriate state based on the type
    // Don't try to send display commands here - the peer may not be connected yet
    switch (type) {
        case MENU_STATE:
            return new MenuState();
        case RELAY_STATE:
            return new RelayState();
        default:
            return nullptr;
    }
}

} // namespace NuggetsInc
