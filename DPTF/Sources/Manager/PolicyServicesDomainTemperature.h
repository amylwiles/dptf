/******************************************************************************
** Copyright (c) 2014 Intel Corporation All Rights Reserved
**
** Licensed under the Apache License, Version 2.0 (the "License"); you may not
** use this file except in compliance with the License.
**
** You may obtain a copy of the License at
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
** WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**
** See the License for the specific language governing permissions and
** limitations under the License.
**
******************************************************************************/

#pragma once

#include "Dptf.h"
#include "PolicyServices.h"
#include "DomainTemperatureInterface.h"

class PolicyServicesDomainTemperature final : public PolicyServices, public DomainTemperatureInterface
{
public:

    PolicyServicesDomainTemperature(DptfManager* dptfManager, UIntN policyIndex);

    virtual TemperatureStatus getTemperatureStatus(UIntN participantIndex, UIntN domainIndex) override final;
    virtual TemperatureThresholds getTemperatureThresholds(UIntN participantIndex, UIntN domainIndex) override final;
    virtual void setTemperatureThresholds(UIntN participantIndex, UIntN domainIndex,
        const TemperatureThresholds& temperatureThresholds) override final;
};