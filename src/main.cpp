#include "app.h"

#include <chrono>



int main()
{
    VulkanApp engine;
    
    engine.init();

    engine.run();


    engine.cleanup();
}
