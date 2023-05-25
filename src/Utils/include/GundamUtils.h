//
// Created by Adrien Blanchet on 21/05/2023.
//

#ifndef GUNDAM_GUNDAMUTILS_H
#define GUNDAM_GUNDAMUTILS_H


#include "VersionConfig.h" // only included here!

#include "GenericToolbox.h"
#include "GenericToolbox.Root.h"
#include "CmdLineParser.h"

#include <map>
#include <string>
#include <vector>
#include <utility>



namespace GundamUtils {

  // forward version config that was auto generated by CMake
  std::string getVersionStr();
  std::string generateFileName(const CmdLineParser& clp_, const std::vector<std::pair<std::string, std::string>>& appendixDict_);

  // dicts
  inline static const std::map<int, std::string> minuitStatusCodeStr{
      { 0 , "status = 0    : OK" },
      { 1 , "status = 1    : Covariance was mad! Thus made pos defined"},
      { 2 , "status = 2    : Hesse is invalid"},
      { 3 , "status = 3    : Edm is above max"},
      { 4 , "status = 4    : Reached call limit"},
      { 5 , "status = 5    : Any other failure"},
      { -1, "status = -1   : We don't even know what happened"}
  };
  inline static const std::map<int, std::string> hesseStatusCodeStr{
      { 0, "status = 0    : OK" },
      { 1, "status = 1    : Hesse failed"},
      { 2, "status = 2    : Matrix inversion failed"},
      { 3, "status = 3    : Matrix is not pos defined"},
      { -1, "status = -1    : Minimize wasn't called before"}
  };
  inline static const std::map<int, std::string> minosStatusCodeStr{
      { 0, "status = 0    : last MINOS run was succesfull" },
      { 1, "status = 1    : Maximum number of function calls exceeded when running for lower error"},
      { 2, "status = 2    : maximum number of function calls exceeded when running for upper error"},
      { 3, "status = 3    : new minimum found when running for lower error"},
      { 4, "status = 4    : new minimum found when running for upper error"},
      { 5, "status = 5    : any other failure"},
      { -1, "status = -1   : Minos is not run"}
  };

  // 0 not calculated 1 approximated 2 made pos def , 3 accurate
  inline static const std::map<int, std::string> covMatrixStatusCodeStr{
      { 0, "status = 0    : not calculated" },
      { 1, "status = 1    : approximated"},
      { 2, "status = 2    : made pos def"},
      { 3, "status = 3    : accurate"}
  };

}

#endif //GUNDAM_GUNDAMUTILS_H
