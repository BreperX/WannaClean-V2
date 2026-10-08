#pragma once
#include "OperationResult.h"

namespace WannaClean::App
{
    void RenderDoneScreen(const WannaClean::Core::OperationResult& result, bool& detailsRequested, bool detailsVisible);
}
