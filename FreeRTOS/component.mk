INC_DIRS += $(freertos_MAIN)include $(freertos_MAIN)include/$(PLATFORM) $(freertos_MAIN)portable/$(PLATFORM)

# args for passing into compile rule generation
freertos_MAIN = $(freertos_ROOT)Source/
freertos_INC_DIR = $(freertos_MAIN)include $(freertos_MAIN)portable/$(PLATFORM)
freertos_SRC_DIR = $(freertos_MAIN) $(freertos_MAIN)portable/$(PLATFORM)
$(eval $(call component_compile_rules,freertos))
