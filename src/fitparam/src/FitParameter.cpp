//
// Created by Adrien BLANCHET on 21/05/2021.
//

#include "FitParameter.h"

#include "Logger.h"

FitParameter::FitParameter() {
  Logger::setUserHeaderStr("[FitParameter]");
  this->reset();
}
FitParameter::~FitParameter() {
  this->reset();
}

void FitParameter::reset() {
  _currentDialSetPtr_ = nullptr;
  _dialSetList_.clear();
  _parameterDefinitionsConfig_ = nlohmann::json();
  _parameterValue_ = 0;
  _parameterIndex_ = -1;
}

void FitParameter::initialize() {

  for( const auto& json : _parameterDefinitionsConfig_ ){
    _dialSetList_.emplace_back();
    _dialSetList_.back().setDialSetConfig(json);
    _dialSetList_.back().setParameterIndex(_parameterIndex_);
    _dialSetList_.back().initialize();
  }

}

void FitParameter::setParameterDefinitionsConfig(const nlohmann::json &jsonConfig) {
  _parameterDefinitionsConfig_ = jsonConfig;
}
void FitParameter::setParameterIndex(int parameterIndex) {
  _parameterIndex_ = parameterIndex;
}
void FitParameter::setName(const std::string &name) {
  _name_ = name;
}
void FitParameter::setParameterValue(double parameterValue) {
  _parameterValue_ = parameterValue;
}

const std::string &FitParameter::getName() const {
  return _name_;
}
double FitParameter::getParameterValue() const {
  return _parameterValue_;
}

void FitParameter::selectDialSet(const std::string &dataSetName_) {

  _currentDialSetPtr_ = nullptr;
  for( auto& dialSet : _dialSetList_ ){
    if( dialSet.getName() == dataSetName_ ){
      _currentDialSetPtr_ = &dialSet;
      return;
    }
  }

  // If not found, find general dialSet
  for( auto& dialSet : _dialSetList_ ){
    if( dialSet.getName().empty() or dialSet.getName() == "*" ){
      _currentDialSetPtr_ = &dialSet;
      return;
    }
  }

}
void FitParameter::reweightEvent(AnaEvent *eventPtr_) {

  if( _currentDialSetPtr_ == nullptr ){
    return;
  }

  _currentDialSetPtr_->reweightEvent(eventPtr_);

}







