//
// Created by Nadrino on 21/05/2021.
//

#include "Parameter.h"
#include "ParameterSet.h"
#include "ConfigUtils.h"

#include "GenericToolbox.Json.h"
#include "Logger.h"

#include "GundamBacktrace.h"

#include <sstream>

#ifndef DISABLE_USER_HEADER
LoggerInit([]{ Logger::setUserHeaderStr("[Parameter]"); });
#endif

void Parameter::readConfigImpl(){
  if( not _parameterConfig_.empty() ){
    _isEnabled_ = GenericToolbox::Json::fetchValue(_parameterConfig_, "isEnabled", true);
    if( not _isEnabled_ ) { return; }

    _isFixed_ = GenericToolbox::Json::fetchValue(_parameterConfig_, "isFixed", _isFixed_);

    auto priorTypeStr = GenericToolbox::Json::fetchValue(_parameterConfig_, "priorType", "");
    if( not priorTypeStr.empty() ){
      _priorType_ = PriorType::toEnum( priorTypeStr, true );
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
      double stepSize(GenericToolbox::Json::fetchValue<double>(_parameterConfig_, "parameterStepSize"));
      LogWarning << "Using step size: " << stepSize << std::endl;
      this->setStepSize( stepSize );
    }

    if( GenericToolbox::Json::doKeyExist(_parameterConfig_, "physicalLimits") ){
        auto physLimits = GenericToolbox::Json::fetchValue(_parameterConfig_, "physicalLimits", JsonType());
        _minPhysical_ = GenericToolbox::Json::fetchValue(physLimits, "minValue", std::nan("UNSET"));
        _maxPhysical_ = GenericToolbox::Json::fetchValue(physLimits, "maxValue", std::nan("UNSET"));
    }

    _dialDefinitionsList_ = GenericToolbox::Json::fetchValue(_parameterConfig_, "dialSetDefinitions", _dialDefinitionsList_);
  }
}
void Parameter::initializeImpl() {
  LogThrowIf(_owner_ == nullptr, "Parameter set ref is not set.");
  LogThrowIf(_parameterIndex_ == -1, "Parameter index is not set.");

  if( not _isEnabled_ ) { return; }
  LogThrowIf(std::isnan(_priorValue_), "Prior value is not set.");
  LogThrowIf(std::isnan(_stdDevValue_), "Std dev value is not set.");
  LogThrowIf(std::isnan(_parameterValue_), "Parameter value is not set.");
}

void Parameter::setMinMirror(double minMirror) {
  if (std::isfinite(_minMirror_) and std::abs(_minMirror_-minMirror) > 1E-6) {
    LogWarning << "Minimum mirror bound changed for " << getFullTitle()
               << " old: " << _minMirror_
               << " new: " << minMirror
               << std::endl;
  }
  _minMirror_ = minMirror;
}
void Parameter::setMaxMirror(double maxMirror) {
  if (std::isfinite(_maxMirror_) and std::abs(_maxMirror_-maxMirror) > 1E-6) {
    LogWarning << "Maximum mirror bound changed for " << getFullTitle()
               << " old: " << _maxMirror_
               << " new: " << maxMirror
               << std::endl;
  }
  _maxMirror_ = maxMirror;
}
void Parameter::setParameterValue(double parameterValue, bool force) {
  if (not isInDomain(parameterValue, true)) {
    LogError << "New parameter value is not in domain: " << parameterValue
             << std::endl;
    LogError << GundamUtils::Backtrace;
    if (not force) std::exit(EXIT_FAILURE);
    else LogAlert << "Forced continuation with invalid parameter" << std::endl;
  }
  if( _parameterValue_ != parameterValue ){
    _gotUpdated_ = true;
    _parameterValue_ = parameterValue;
  }
  else{ _gotUpdated_ = false; }
}
double Parameter::getParameterValue() const {
  if ( not isValueWithinBounds() ) {
    LogWarning << "Getting out of bounds parameter: "
               << getSummary() << std::endl;
    LogDebug << GundamUtils::Backtrace;
  }
  return _parameterValue_;
}
void Parameter::setDialSetConfig(const JsonType &jsonConfig_) {
  auto jsonConfig = jsonConfig_;
  while( jsonConfig.is_string() ){
    LogWarning << "Forwarding FitParameterSet config to: \"" << jsonConfig.get<std::string>() << "\"..." << std::endl;
    jsonConfig = ConfigUtils::readConfigFile(jsonConfig.get<std::string>());
  }
  _dialDefinitionsList_ = jsonConfig.get<std::vector<JsonType>>();
}
void Parameter::setParameterDefinitionConfig(const JsonType &config_){
  _parameterConfig_ = config_;
}

void Parameter::setValueAtPrior(){
  setParameterValue(getPriorValue());
}
void Parameter::setCurrentValueAsPrior(){
  setPriorValue(getParameterValue());
}
bool Parameter::isInDomain(double value_, bool verbose_) const {
  if( std::isnan(value_) ) {
    if (verbose_) {
      LogError << "NaN value is not in parameter domain" << std::endl;
      LogError << "Summary: " << getSummary() << std::endl;
    }
    return false;
  }
  if ( not std::isnan(_minValue_) and value_ < _minValue_ ) {
    if (verbose_) {
      LogError << "Value is below minimum: " << value_
               << std::endl;
      LogError << "Summary: " << getSummary() << std::endl;
    }
    return false;
  }
  if ( not std::isnan(_maxValue_) and value_ > _maxValue_ ) {
    if (verbose_) {
      LogError << "Attempting to set parameter above the maximum"
               << " -- New value: " << value_
               << std::endl;
      LogError << "Summary: " << getSummary() << std::endl;
    }
    return false;
  }
  return true;
}
bool Parameter::isPhysical(double value_) const {
  if (not isInDomain(value_)) return false;
  if ( not std::isnan(_minPhysical_) and value_ < _minPhysical_ ) return false;
  if ( not std::isnan(_maxPhysical_) and value_ > _maxPhysical_ ) return false;
  return true;
}
bool Parameter::isMirrored(double value_) const {
  if ( not std::isnan(_minMirror_) and value_ < _minMirror_ ) return true;
  if ( not std::isnan(_maxMirror_) and value_ > _maxMirror_ ) return true;
  return false;
}
bool Parameter::isValidValue(double value) const {
  if ((_validFlags_ & 0b0001)!=0 and (not isInDomain(value))) return false;
  if ((_validFlags_ & 0b0010)!=0 and (isMirrored(value))) return false;
  if ((_validFlags_ & 0b0100)!=0 and (not isPhysical(value))) return false;
  return true;
}
void Parameter::setValidity(const std::string& validity) {
  if (validity.find("noran") != std::string::npos) _validFlags_ &= ~0b0001;
  else if (validity.find("ran") != std::string::npos) _validFlags_ |= 0b0001;
  if (validity.find("nomir") != std::string::npos) _validFlags_ &= ~0b0010;
  else if (validity.find("mir") != std::string::npos) _validFlags_ |= 0b0010;
  if (validity.find("nophy") != std::string::npos) _validFlags_ &= ~0b0100;
  else if (validity.find("phy") != std::string::npos) _validFlags_ |= 0b0100;
}
bool Parameter::isValueWithinBounds() const{
  return isInDomain(_parameterValue_);
}
double Parameter::getDistanceFromNominal() const{
  return (getParameterValue() - getPriorValue()) / _stdDevValue_;
}
std::string Parameter::getTitle() const {
  std::stringstream ss;
  ss << "#" << _parameterIndex_;
  if( not _name_.empty() ) ss << "_" << _name_;
  return ss.str();
}
std::string Parameter::getFullTitle() const{
  return _owner_->getName() + "/" + this->getTitle();
}
std::string Parameter::getSummary(bool shallow_) const {
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
  if (not isValueWithinBounds()) ss << " out of bounds";

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
// End:
