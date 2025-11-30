#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
from nv_attestation_sdk import attestation
import sys
import os
import json
import binascii

client = attestation.Attestation()
client.set_name("attest")
client.set_nonce(binascii.b2a_hex(os.urandom(32)).decode('utf-8'))
client.set_claims_version("3.0")

print ("[gpu_attest] node name :", client.get_name())
file = "attest_policy.json"

client.add_verifier(attestation.Devices.GPU, attestation.Environment.LOCAL, "", "")

print(client.get_verifiers())

print ("[gpu_attest] call get_evidence()")
evidence_list = client.get_evidence()

print ("[gpu_attest] call attest() - expecting True")
attest_result = client.attest(evidence_list)
print("[gpu_attest] call attest() - result : ", attest_result)
if not attest_result:
    sys.exit(1)

print ("[gpu_attest] token : "+str(client.get_token()))
print ("[gpu_attest] call validate_token() - expecting True")

with open(os.path.join(os.path.dirname(__file__), file)) as json_file:
    json_data = json.load(json_file)
    att_result_policy = json.dumps(json_data)

validation_result = client.validate_token(att_result_policy)
print ("[gpu_attest] call validate_token() - result: ", validation_result)
if not validation_result:
    sys.exit(1)

client.decode_token(client.get_token())
