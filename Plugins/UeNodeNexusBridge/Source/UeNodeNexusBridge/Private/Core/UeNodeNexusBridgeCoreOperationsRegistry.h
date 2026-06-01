#pragma once

namespace UeNodeNexusBridge
{
// Wire every core operation handler into the shared operation registry. Called
// once at module startup so core, AutoIndex, and plugin operations all dispatch
// through one mechanism instead of a hand-written if-chain.
void RegisterCoreOperations();
void UnregisterCoreOperations();
}
