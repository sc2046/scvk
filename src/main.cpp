#include "app.h"

#include <chrono>



int main()
{
    VulkanApp engine;
    
    engine.init();
    engine.initAllocators();

    engine.run();


    engine.cleanup();
}
