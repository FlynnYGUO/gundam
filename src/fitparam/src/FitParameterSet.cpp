//
// Created by Adrien BLANCHET on 21/05/2021.
//

#include "FitParameterSet.h"
#include "JsonUtils.h"

#include "GenericToolbox.h"
#include "GenericToolboxRootExt.h"
#include "Logger.h"

LoggerInit([](){
  Logger::setUserHeaderStr("[FitParameterSet]");
} )

FitParameterSet::FitParameterSet() {
  this->reset();
}
FitParameterSet::~FitParameterSet() {
  this->reset();
}

void FitParameterSet::reset() {

  _isInitialized_ = false;

  delete _covarianceMatrix_; _covarianceMatrix_ = nullptr;
  delete _covarianceMatrixFile_; _covarianceMatrixFile_ = nullptr;

  _jsonConfig_ = nlohmann::json();
  _parameterList_.clear();

}

void FitParameterSet::setJsonConfig(const nlohmann::json &jsonConfig) {
  _jsonConfig_ = jsonConfig;
}

void FitParameterSet::initialize() {

  if( _jsonConfig_.empty() ){
    LogError << "Json config not set" << std::endl;
    throw std::logic_error("json config not set");
  }

  _name_ = JsonUtils::fetchValue<std::string>(_jsonConfig_, "name");
  LogInfo << "Initializing parameter set: " << _name_ << std::endl;

  _isEnabled_ = JsonUtils::fetchValue<bool>(_jsonConfig_, "isEnabled");
  if( not _isEnabled_ ){
    LogWarning << _name_ << " parameters are disabled. Skipping config parsing..." << std::endl;
    return;
  }

  _covarianceMatrixFile_ = TFile::Open(JsonUtils::fetchValue<std::string>(_jsonConfig_, "covarianceMatrixFilePath").c_str());
  if( not _covarianceMatrixFile_->IsOpen() ){
    LogError << "Could not open: _covarianceMatrixFile_: " << _covarianceMatrixFile_->GetPath() << std::endl;
    throw std::runtime_error("Could not open: _covarianceMatrixFile_");
  }

  _covarianceMatrix_ = (TMatrixDSym*) _covarianceMatrixFile_->Get(
    JsonUtils::fetchValue<std::string>(_jsonConfig_, "covarianceMatrixTMatrixD").c_str()
    );
  if( _covarianceMatrix_ == nullptr ){
    LogError << "Could not find: " << JsonUtils::fetchValue<std::string>(_jsonConfig_, "covarianceMatrixTObjectPath")
      << " in " << _covarianceMatrixFile_->GetPath() << std::endl;
    throw std::runtime_error("Could not find: covarianceMatrixTObjectPath");
  }

  _correlationMatrix_ = GenericToolbox::convertToSymmetricMatrix(GenericToolbox::convertToCorrelationMatrix((TMatrixD*) _covarianceMatrix_));

  LogInfo << "Parameter set \"" << _name_ << "\" is handling " << _covarianceMatrix_->GetNcols() << " parameters." << std::endl;

  // Optional parameters:
  std::string pathBuffer;

  // parameterPriorTVectorD
  pathBuffer = JsonUtils::fetchValue<std::string>(_jsonConfig_, "parameterPriorTVectorD", "");
  if(not pathBuffer.empty()){
    LogDebug << "Reading provided parameterPriorTVectorD..." << std::endl;
    _parameterPriorList_ = (TVectorT<double>*) _covarianceMatrixFile_->Get(pathBuffer.c_str());
    // Sanity checks
    if( _parameterPriorList_ == nullptr ){
      LogError << "Could not find \"" << pathBuffer << "\" into \"" << _covarianceMatrixFile_->GetName() << "\"" << std::endl;
      throw std::runtime_error("TObject not found.");
    }
    else if( _parameterPriorList_->GetNrows() != _covarianceMatrix_->GetNrows() ){
      LogError << GET_VAR_NAME_VALUE(_parameterPriorList_->GetNrows() != _covarianceMatrix_->GetNrows()) << std::endl;
      throw std::runtime_error("TObject size mismatch.");
    }
  }
  else{
    LogDebug << "No parameterPriorTVectorD provided, all parameter prior are set to 1." << std::endl;
    _parameterPriorList_ = new TVectorT<double>(_covarianceMatrix_->GetNrows());
    for( int iPar = 0 ; iPar < _parameterPriorList_->GetNrows() ; iPar++ ){
      (*_parameterPriorList_)[iPar] = 1;
    }
  }

  // parameterNameTObjArray
  pathBuffer = JsonUtils::fetchValue<std::string>(_jsonConfig_, "parameterNameTObjArray", "");
  if(not pathBuffer.empty()){
    LogDebug << "Reading provided parameterNameTObjArray..." << std::endl;
    _parameterNamesList_ = (TObjArray*) _covarianceMatrixFile_->Get(pathBuffer.c_str());
    // Sanity checks
    if( _parameterNamesList_ == nullptr ){
      LogError << "Could not find \"" << pathBuffer << "\" into \"" << _covarianceMatrixFile_->GetName() << "\"" << std::endl;
      throw std::runtime_error("TObject not found.");
    }
    else if( _parameterNamesList_->GetSize() != _covarianceMatrix_->GetNrows() ){
      LogError << GET_VAR_NAME_VALUE(_parameterNamesList_->GetSize() != _covarianceMatrix_->GetNrows()) << std::endl;
      throw std::runtime_error("TObject size mismatch.");
    }
  }
  else{
    LogDebug << "No parameterNameTObjArray provided, parameters will be referenced with their index." << std::endl;
    _parameterNamesList_ = new TObjArray(_covarianceMatrix_->GetNrows());
    for( int iPar = 0 ; iPar < _parameterPriorList_->GetNrows() ; iPar++ ){
      _parameterNamesList_->Add(new TNamed("", ""));
    }
  }

  std::vector<nlohmann::json> dialSetDefinitions = JsonUtils::fetchValue<std::vector<nlohmann::json>>(_jsonConfig_, "dialSetDefinitions");

  for( int iPararmeter = 0 ; iPararmeter < _covarianceMatrix_->GetNcols() ; iPararmeter++ ){
    _parameterList_.emplace_back();
    _parameterList_.back().setParameterIndex(iPararmeter);
    _parameterList_.back().setName(_parameterNamesList_->At(iPararmeter)->GetName());
    _parameterList_.back().setParameterValue((*_parameterPriorList_)[iPararmeter]);
    _parameterList_.back().setPriorValue((*_parameterPriorList_)[iPararmeter]);
    _parameterList_.back().setStdDevValue((*_covarianceMatrix_)[iPararmeter][iPararmeter]);
    _parameterList_.back().setDialSetConfigs(dialSetDefinitions);
    _parameterList_.back().initialize();
  }

  _isInitialized_ = true;
}

std::vector<FitParameter> &FitParameterSet::getParameterList() {
  return _parameterList_;
}

void FitParameterSet::reweightEvent(AnaEvent *eventPtr_) {
  this->passIfInitialized(__METHOD_NAME__);

  double totalWeight = 1;

  for( auto& parameter : _parameterList_ ){
    if( parameter.getCurrentDialSetPtr() == nullptr ) continue;

    int dialIndex = parameter.getCurrentDialSetPtr()->getDialIndex(eventPtr_);
    if( dialIndex == -1 ) continue;

    totalWeight *= parameter.getCurrentDialSetPtr()->getDialList().at( dialIndex )->evalResponse( parameter.getParameterValue() );
  }

  eventPtr_->AddEvWght(totalWeight);
}

void FitParameterSet::passIfInitialized(const std::string &methodName_) const {
  if( not _isInitialized_ ){
    LogError << "Can't do \"" << methodName_ << "\" while not initialized." << std::endl;
    throw std::logic_error("class not initialized");
  }
}

std::string FitParameterSet::getSummary() {
  std::stringstream ss;

  ss << __CLASS_NAME__ << ": " << this << std::endl;
  ss << GET_VAR_NAME_VALUE(_isInitialized_) << std::endl;
  ss << GET_VAR_NAME_VALUE(_name_) << std::endl;
  ss << GET_VAR_NAME_VALUE(_covarianceMatrix_->GetNrows()) << std::endl;
  ss << GET_VAR_NAME_VALUE(_parameterList_.size()) << std::endl;
  ss << GET_VAR_NAME_VALUE(_isEnabled_);

  if( not _parameterList_.empty() ){
    ss << std::endl << "Parameters:";
    for( int iPar = 0 ; iPar < _covarianceMatrix_->GetNrows() ; iPar++ ){
      ss << std::endl << GenericToolbox::indentString(_parameterList_.at(iPar).getSummary(), 2);
    }
  }


  return ss.str();
}


