//
// Created by Adrien Blanchet on 29/11/2022.
//

#include "DialInterface.h"
#include "GlobalVariables.h"

#include "Logger.h"

LoggerInit([]{
  Logger::setUserHeaderStr("[DialInterface]");
});

void DialInterface::setDialBaseRef(DialBase *dialBasePtr) {
  _dialBaseRef_ = dialBasePtr;
}
void DialInterface::setInputBufferRef(DialInputBuffer *inputBufferRef) {
  _inputBufferRef_ = inputBufferRef;
}
void DialInterface::setResponseSupervisorRef(const DialResponseSupervisor *responseSupervisorRef) {
  _responseSupervisorRef_ = responseSupervisorRef;
}
void DialInterface::setDialBinRef(const DataBin *dialBinRef) {
  _dialBinRef_ = dialBinRef;
}

double DialInterface::evalResponse(){
  if( _inputBufferRef_->isMasked() ) return 1;

  double response = _dialBaseRef_->evalResponse( *_inputBufferRef_ );

  if(_responseSupervisorRef_ != nullptr ){
    _responseSupervisorRef_->process(response);
  }

  return response;
}
std::string DialInterface::getSummary(bool shallow_) {
  std::stringstream ss;
  ss << _dialBaseRef_->getDialTypeName() << ":";

  if( _inputBufferRef_ != nullptr ){
    ss << " input(" << _inputBufferRef_->getSummary() << ")";
  }

  // apply on?
  if( _dialBinRef_ != nullptr ){
    ss << " applyOn(" << _dialBinRef_->getSummary() << ")";
  }

  if( _responseSupervisorRef_ != nullptr ){
    ss << " supervisor(" << _responseSupervisorRef_->getSummary() << ")";
  }

  ss << " response=" << this->evalResponse();

  return ss.str();
}

