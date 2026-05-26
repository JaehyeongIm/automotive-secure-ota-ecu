################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Lib/sha256.c \
../Core/Lib/uECC.c 

OBJS += \
./Core/Lib/sha256.o \
./Core/Lib/uECC.o 

C_DEPS += \
./Core/Lib/sha256.d \
./Core/Lib/uECC.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Lib/%.o Core/Lib/%.su Core/Lib/%.cyclo: ../Core/Lib/%.c Core/Lib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Lib

clean-Core-2f-Lib:
	-$(RM) ./Core/Lib/sha256.cyclo ./Core/Lib/sha256.d ./Core/Lib/sha256.o ./Core/Lib/sha256.su ./Core/Lib/uECC.cyclo ./Core/Lib/uECC.d ./Core/Lib/uECC.o ./Core/Lib/uECC.su

.PHONY: clean-Core-2f-Lib

