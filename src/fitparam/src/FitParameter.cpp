//
// Created by Adrien BLANCHET on 21/05/2021.
//

#include "sstream"

#include "Logger.h"

#include "JsonUtils.h"
#include "FitParameter.h"

LoggerInit([](){
  Logger::setUserHeaderStr("[FitParameter]");
} )

FitParameter::FitParameter() {
  this->reset();
}
FitParameter::~FitParameter() {
  this->reset();
}

void FitParameter::reset() {
  _currentDialSetPtr_ = nullptr;
  _dialSetList_.clear();
  _dialDefinitionsList_ = nlohmann::json();
  _parameterIndex_ = -1;
  _parameterValue_ = std::numeric_limits<double>::quiet_NaN();
  _priorValue_     = std::numeric_limits<double>::quiet_NaN();
  _stdDevValue_    = std::numeric_limits<double>::quiet_NaN();
}

void FitParameter::initialize() {

  if     ( _priorValue_ == std::numeric_limits<double>::quiet_NaN() ){
    LogError << "_priorValue_ is not set." << std::endl;
    throw std::logic_error("_priorValue_ is not set.");
  }
  else if( _stdDevValue_ == std::numeric_limits<double>::quiet_NaN() ){
    LogError << "_stdDevValue_ is not set." << std::endl;
    throw std::logic_error("_stdDevValue_ is not set.");
  }
  else if( _parameterValue_ == std::numeric_limits<double>::quiet_NaN() ){
    LogError << "_parameterValue_ is not set." << std::endl;
    throw std::logic_error("_parameterValue_ is not set.");
  }
  else if( _parameterIndex_ == -1 ){
    LogError << "_parameterIndex_ is not set." << std::endl;
    throw std::logic_error("_parameterIndex_ is not set.");
  }
  else if( _dialDefinitionsList_.empty() ){
    LogError << "_dialDefinitionsList_ is not set." << std::endl;
    throw std::logic_error("_dialDefinitionsList_ is not set.");
  }

  LogDebug << "Initializing FitParameter #" << _parameterIndex_;
  if( not _name_.empty() ) LogDebug << " (" << _name_ << ")";
  LogDebug << std::endl;

  for( const auto& dialDefinitionConfig : _dialDefinitionsList_ ){
    _dialSetList_.emplace_back();
    _dialSetList_.back().setParameterIndex(_parameterIndex_);
    _dialSetList_.back().setDialSetConfig(dialDefinitionConfig);
    _dialSetList_.back().initialize();
  }

}

void FitParameter::setDialSetConfigs(const std::vector<nlohmann::json> &jsonConfigList) {
  _dialDefinitionsList_ = jsonConfigList;
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
void FitParameter::setPriorValue(double priorValue) {
  _priorValue_ = priorValue;
}
void FitParameter::setStdDevValue(double stdDevValue) {
  _stdDevValue_ = stdDevValue;
}

const std::string &FitParameter::getName() const {
  return _name_;
}
double FitParameter::getParameterValue() const {
  return _parameterValue_;
}
DialSet *FitParameter::getCurrentDialSetPtr() const {
  return _currentDialSetPtr_;
}
int FitParameter::getParameterIndex() const {
  return _parameterIndex_;
}

void FitParameter::selectDialSet(const std::string &dataSetName_) {

  _currentDialSetPtr_ = nullptr;
  for( auto& dialSet : _dialSetList_ ){
    int dialIndex = GenericToolbox::doesElementIsInVector(dataSetName_, dialSet.getDataSetNameList());
    if( dialIndex != -1 ){
      _currentDialSetPtr_ = &dialSet;
      return;
    }
  }

  // If not found, find general dialSet
  for( auto& dialSet : _dialSetList_ ){
    if(    GenericToolbox::doesElementIsInVector("", dialSet.getDataSetNameList())
        or GenericToolbox::doesElementIsInVector("*", dialSet.getDataSetNameList())
      ){
      _currentDialSetPtr_ = &dialSet;
      return;
    }
  }

}
void FitParameter::reweightEvent(AnaEvent *eventPtr_) {

  if( _currentDialSetPtr_ == nullptr ){
    return;
  }
  int index = _currentDialSetPtr_->getDialIndex(eventPtr_);
  if( index == -1 ) return;
  eventPtr_->AddEvWght(_currentDialSetPtr_->getDialList().at(index )->evalResponse(_parameterValue_ ));

}

std::string FitParameter::getSummary() {
  std::stringstream ss;

  ss << "#" << _parameterIndex_;
  if( not _name_.empty() ) ss << " (" << _name_ << ")";
  ss << ": " << GET_VAR_NAME_VALUE(_priorValue_);
  ss << ", " << GET_VAR_NAME_VALUE(_stdDevValue_);
  ss << ", " << GET_VAR_NAME_VALUE(_parameterValue_);

  if( not _dialSetList_.empty() ){
    ss << std::endl << "DialSets:";
    for( const auto& dialSet : _dialSetList_ ){
      ss << std::endl << GenericToolbox::indentString(dialSet.getSummary(), 2);
    }
  }

  return ss.str();
}









