#!/usr/bin/python3
#
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: Apache 2.0
#

import sys
sys.path.append("..")
from test_helper import *

skip()


net_folder = "network_csvs/"
sch_folder = "POC_config_csvs/"

root_file = "test_resnet"

p = os.path.dirname(os.path.realpath(__file__))

model = generate_model(os.path.join(p,net_folder)+root_file+".py")
graphfile, s_result, _ = compile_graphFile(model, 4, 5, os.path.join(p, "../../../config/resnet50_strategy.csv"))

t_result = execute_network(graphfile)
validate_files(s_result, t_result)