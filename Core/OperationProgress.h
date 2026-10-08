#pragma once

#include <functional>
#include <string>

namespace WannaClean::Core
{
    using OperationProgressCallback = std::function<void(float, const std::string&)>;
}
