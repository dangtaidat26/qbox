/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <pass.h>

typedef gs::pass<> pass;

void module_register() { GSC_MODULE_REGISTER_C(pass); }
