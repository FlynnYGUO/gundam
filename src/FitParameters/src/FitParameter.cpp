//
// Created by Nadrino on 21/05/2021.
//

#include "FitParameter.h"
#include "FitParameterSet.h"
#include "ConfigUtils.h"

#include "GenericToolbox.Json.h"
#include "Logger.h"

#include <sstream>

LoggerInit([]{ Logger::setUserHeaderStr("[FitParameter]"); });


void FitParameter::readConfigImpl(){
  if( not _parameterConfig_.empty() ){
    _isEnabled_ = GenericToolbox::Json::fetchValue(_parameterConfig_, "isEnabled", true);
    if( not _isEnabled_ ) { return; }

    auto priorTypeStr = GenericToolbox::Json::fetchValue(_parameterConfig_, "priorType", "");
    if( not priorTypeStr.empty() ){
      _priorType_ = PriorType::PriorTypeEnumNamespace::toEnum(priorTypeStr);
      if( _priorType_ == PriorType::Flat ){ _isFree_ = true; }
    }

    if( GenericToolbox::Json::doKeyExist(_parameterConfig_, "priorValue") ){
      double priorOverride = GenericToolbox::Json::fetchValue(_parameterConfig_, "priorValue", this->getPriorValue());
      if( not std::isnan(priorOverride) ){
        LogWarning << this->getTitle() << ": prior value override -> " << priorOverride << std::endl;
        this->setPriorValue(priorOverride);
        this->setParameterValue(priorOverride);
      }
    }

    if( GenericToolbox::Json::doKeyExist(_parameterConfig_, "parameterLimits") ){
      std::pair<double, double> limits{std::nan(""), std::nan("")};
      limits = GenericToolbox::Json::fetchValue(_parameterConfig_, "parameterLimits", limits);
      LogWarning << "Overriding parameter limits: [" << limits.first << ", " << limits.second << "]." << std::endl;
      this->setMinValue(limits.first);
      this->setMaxValue(limits.second);
    }

    if( GenericToolbox::Json::doKeyExist(_parameterConfig_, "parameterStepSize") ){
      double stepSize{GenericToolbox::Json::fetchValue<double>(_parameterConfig_, "parameterStepSize")};
      LogWarning << "Using step size: " << stepSize << std::endl;
      this->setStepSize( stepSize );
    }

    if( GenericToolbox::Json::doKeyExist(_parameterConfig_, "physicalLimits") ){
        auto physLimits = GenericToolbox::Json::fetchValue(_parameterConfig_, "physicalLimits", nlohmann::json());
        _minPhysical_ = GenericToolbox::Json::fetchValue(physLimits, "minValue", std::nan("UNSET"));
        _maxPhysical_ = GenericToolbox::Json::fetchValue(physLimits, "maxValue", std::nan("UNSET"));
    }

    _dialDefinitionsList_ = GenericToolbox::Json::fetchValue(_parameterConfig_, "dialSetDefinitions", _dialDefinitionsList_);
  }
}
void FitParameter::initializeImpl() {
  LogThrowIf(_owner_ == nullptr, "Parameter set ref is not set.");
  LogThrowIf(_parameterIndex_ == -1, "Parameter index is not set.");

  if( not _isEnabled_ ) { return; }
  LogThrowIf(std::isnan(_priorValue_), "Prior value is not set.");
  LogThrowIf(std::isnan(_stdDevValue_), "Std dev value is not set.");
  LogThrowIf(std::isnan(_parameterValue_), "Parameter value is not set.");
}

void FitParameter::setMinMirror(double minMirror) {
  if (std::isfinite(_minMirror_) and std::abs(_minMirror_-minMirror) > 1E-6) {
    LogWarning << "Minimum mirror bound changed for " << getFullTitle()
               << " old: " << _minMirror_
               << " new: " << minMirror
               << std::endl;
  }
  _minMirror_ = minMirror;
}
void FitParameter::setMaxMirror(double maxMirror) {
  if (std::isfinite(_maxMirror_) and std::abs(_maxMirror_-maxMirror) > 1E-6) {
    LogWarning << "Maximum mirror bound changed for " << getFullTitle()
               << " old: " << _maxMirror_
               << " new: " << maxMirror
               << std::endl;
  }
  _maxMirror_ = maxMirror;
}
void FitParameter::setParameterValue(double parameterValue) {
  LogThrowIf( std::isnan(parameterValue), "Attempting to set NaN value for par:" << std::endl << this->getSummary() );
  if( _parameterValue_ != parameterValue ){
    _gotUpdated_ = true;
    _parameterValue_ = parameterValue;
  }
  else{ _gotUpdated_ = false; }
}
void FitParameter::setDialSetConfig(const nlohmann::json &jsonConfig_) {
  auto jsonConfig = jsonConfig_;
  while( jsonConfig.is_string() ){
    LogWarning << "Forwarding FitParameterSet config to: \"" << jsonConfig.get<std::string>() << "\"..." << std::endl;
    jsonConfig = ConfigUtils::readConfigFile(jsonConfig.get<std::string>());
  }
  _dialDefinitionsList_ = jsonConfig.get<std::vector<nlohmann::json>>();
}
void FitParameter::setParameterDefinitionConfig(const nlohmann::json &config_){
  _parameterConfig_ = config_;
  ConfigUtils::forwardConfig(_parameterConfig_);
}

void FitParameter::setValueAtPrior(){
  _parameterValue_ = _priorValue_;
}
void FitParameter::setCurrentValueAsPrior(){
  _priorValue_ = _parameterValue_;
}

bool FitParameter::isValueWithinBounds() const{
  if( not std::isnan(_minValue_) and _parameterValue_ < _minValue_ ) return false;
  if( not std::isnan(_maxValue_) and _parameterValue_ > _maxValue_ ) return false;
  return true;
}
double FitParameter::getDistanceFromNominal() const{
  return (_parameterValue_ - _priorValue_) / _stdDevValue_;
}
std::string FitParameter::getTitle() const {
  std::stringstream ss;
  ss << "#" << _parameterIndex_;
  if( not _name_.empty() ) ss << "_" << _name_;
  return ss.str();
}
std::string FitParameter::getFullTitle() const{
  return _owner_->getName() + "/" + this->getTitle();
}
std::string FitParameter::getSummary(bool shallow_) const {
  std::stringstream ss;

  ss << this->getFullTitle();
  ss << ", isEnabled=" << _isEnabled_;
  ss << ": value=" << _parameterValue_;
  ss << ", prior=" << _priorValue_;
  ss << ", stdDev=" << _stdDevValue_;
  ss << ", bounds=[ ";
  if( std::isnan(_minValue_) ) ss << "-inf";
  else ss << _minValue_;
  ss << ", ";
  if( std::isnan(_maxValue_) ) ss << "+inf";
  else ss << _maxValue_;
  ss << " ]";

  return ss.str();
}

//  A Lesser GNU Public License

//  Copyright (C) 2023 GUNDAM DEVELOPERS

//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.

//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.

//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the
//
//  Free Software Foundation, Inc.
//  51 Franklin Street, Fifth Floor,
//  Boston, MA  02110-1301  USA

// Local Variables:
// mode:c++
// c-basic-offset:2
// compile-command:"$(git rev-parse --show-toplevel)/cmake/gundam-build.sh"
// End:
