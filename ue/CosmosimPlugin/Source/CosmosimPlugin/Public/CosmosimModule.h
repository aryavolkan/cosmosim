#pragma once

#include "Modules/ModuleManager.h"

class FCosmosimModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void* LibHandle = nullptr;
};
