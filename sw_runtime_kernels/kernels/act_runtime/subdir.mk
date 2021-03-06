#
# Copyright (C) 2022 Intel Corporation.
# SPDX-License-Identifier: Apache 2.0
#

#

include-dirs-shave_nn-y += inc

srcs-shave_nn-y += $(wildcard src/*.c*) $(wildcard src/*.asm)

# CONFIG_TARGET_SOC_* options are mutually exclusive. Only one can be enabled at a time
target-soc-37xx = $(CONFIG_TARGET_SOC_3710)$(CONFIG_TARGET_SOC_3720)
srcs-shave_nn-$(target-soc-37xx) += $(wildcard src/37xx/*.c*) $(wildcard src/37xx/*.asm)
