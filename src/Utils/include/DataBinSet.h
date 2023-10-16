//
// Created by Nadrino on 19/05/2021.
//

#ifndef GUNDAM_DATABINSET_H
#define GUNDAM_DATABINSET_H

#include <vector>
#include <string>

#include "DataBin.h"

class DataBinSet {

public:
  // static
  static void setVerbosity(int maxLogLevel_);

public:
  DataBinSet() = default;

  // setter
  void setName(const std::string &name){ _name_ = name; }

  // const getters
  [[nodiscard]] bool isEmpty() const { return _binsList_.empty(); }
  [[nodiscard]] const std::string &getFilePath() const { return _filePath_; }
  [[nodiscard]] const std::vector<DataBin> &getBinsList() const { return _binsList_; }

  // getters
  std::vector<DataBin> &getBinsList() { return _binsList_; }

  // core
  void readBinningDefinition(const std::string& filePath_);
  [[nodiscard]] std::string getSummary() const;
  [[nodiscard]] std::vector<std::string> buildVariableNameList() const;

private:
  std::string _name_;
  std::string _filePath_;
  std::vector<DataBin> _binsList_{};

};


#endif //GUNDAM_DATABINSET_H
