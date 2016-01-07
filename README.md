# rgbw
This class & driver handles general purpose RGB PWM light strips with an optional fourth White channel. 
The driver class and device are configured using of_properties (device tree) to configure each color as a hard PWM
or a soft PWM. 

A hard PWM is defined as a PWM controlled from a hardware module on the CPU/MPU. A soft PWM is a GPIO pin driven
using hard IRQ context to act as a PWM. The of HR Timers is necessary to achieve low system latency and low resource usages.
