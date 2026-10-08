#pragma once
#include "OperationPlan.h"

namespace WannaClean::App
{
    void RenderAggressiveConfirmDialog(const WannaClean::Core::OperationPlan& plan, bool& confirmed, bool& cancelled);
}
