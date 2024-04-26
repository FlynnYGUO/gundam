
#include "GundamGlobals.h"
#include "GundamApp.h"
#include "GundamUtils.h"
#include "FitterEngine.h"
#include "ConfigUtils.h"

#include "Logger.h"
#include "CmdLineParser.h"
#include "GenericToolbox.Json.h"
#include "GenericToolbox.Root.h"
#include "GenericToolbox.Utils.h"

#include <TFile.h>
#include "TH1D.h"
#include "TH2D.h"

#include <string>
#include <vector>


LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

void readBinningFromFile(const char* filename, std::vector<Double_t>& binEdges) ;


int main(int argc, char** argv){

  using namespace GundamUtils;

  GundamApp app{"cross-section calculator tool"};

  // --------------------------
  // Read Command Line Args:
  // --------------------------
  CmdLineParser clParser;

  clParser.addDummyOption("Main options:");
  clParser.addOption("configFile", {"-c", "--config-file"}, "Specify path to the fitter config file");
  clParser.addOption("fitterFile", {"-f"}, "Specify the fitter output file");
  clParser.addOption("outputFile", {"-o", "--out-file"}, "Specify the CalcXsec output file");
  clParser.addOption("nbThreads", {"-t", "--nb-threads"}, "Specify nb of parallel threads");
  clParser.addOption("nToys", {"-n"}, "Specify number of toys");
  clParser.addOption("randomSeed", {"-s", "--seed"}, "Set random seed");

  clParser.addDummyOption("Trigger options:");
  clParser.addTriggerOption("dryRun", {"-d", "--dry-run"}, "Only overrides fitter config and print it.");
  clParser.addTriggerOption("useBfAsXsec", {"--use-bf-as-xsec"}, "Use best-fit as x-sec value instead of mean of toys.");
  clParser.addTriggerOption("usePreFit", {"--use-prefit"}, "Use prefit covariance matrices for the toy throws.");

  LogInfo << "Usage: " << std::endl;
  LogInfo << clParser.getConfigSummary() << std::endl << std::endl;

  clParser.parseCmdLine(argc, argv);

  LogThrowIf(clParser.isNoOptionTriggered(), "No option was provided.");

  LogInfo << "Provided arguments: " << std::endl;
  LogInfo << clParser.getValueSummary() << std::endl << std::endl;


  // Sanity checks
  LogThrowIf(not clParser.isOptionTriggered("configFile"), "Xsec calculator config file not provided.");
  LogThrowIf(not clParser.isOptionTriggered("fitterFile"), "Did not provide the output fitter file.");
  LogThrowIf(not clParser.isOptionTriggered("nToys"), "Did not provide number of toys.");


  // Global parameters
  gRandom = new TRandom3(0);     // Initialize with a UUID
  if( clParser.isOptionTriggered("randomSeed") ){
    LogAlert << "Using user-specified random seed: " << clParser.getOptionVal<ULong_t>("randomSeed") << std::endl;
    gRandom->SetSeed(clParser.getOptionVal<ULong_t>("randomSeed"));
  }
  else{
    ULong_t seed = time(nullptr);
    LogInfo << "Using \"time(nullptr)\" random seed: " << seed << std::endl;
    gRandom->SetSeed(seed);
  }

  GundamGlobals::getParallelWorker().setNThreads( clParser.getOptionVal("nbThreads", 1) );
  LogInfo << "Running the fitter with " << GundamGlobals::getParallelWorker().getNbThreads() << " parallel threads." << std::endl;

  // Reading fitter file
  std::string fitterFile{clParser.getOptionVal<std::string>("fitterFile")};
  std::unique_ptr<TFile> fitterRootFile{nullptr};
  JsonType fitterConfig; // will be used to load the propagator

  if( GenericToolbox::hasExtension(fitterFile, "root") ){
    LogWarning << "Opening fitter output file: " << fitterFile << std::endl;
    fitterRootFile = std::unique_ptr<TFile>( TFile::Open( fitterFile.c_str() ) );
    LogThrowIf( fitterRootFile == nullptr, "Could not open fitter output file." );

    ObjectReader::throwIfNotFound = true;

    ObjectReader::readObject<TNamed>(fitterRootFile.get(), {{"gundam/config_TNamed"}, {"gundamFitter/unfoldedConfig_TNamed"}}, [&](TNamed* config_){
      fitterConfig = GenericToolbox::Json::readConfigJsonStr( config_->GetTitle() );
    });
  }
  else{
    LogWarning << "Reading fitter config file: " << fitterFile << std::endl;
    fitterConfig = GenericToolbox::Json::readConfigFile( fitterFile );

    clParser.getOptionPtr("usePreFit")->setIsTriggered( true );
  }

  LogAlertIf(clParser.isOptionTriggered("usePreFit")) << "Pre-fit mode enabled: will throw toys according to the prior covariance matrices..." << std::endl;

  ConfigUtils::ConfigHandler cHandler{ fitterConfig };

  // Disabling defined fit samples:
  LogInfo << "Removing defined samples..." << std::endl;
  ConfigUtils::applyOverrides(
      cHandler.getConfig(),
      GenericToolbox::Json::readConfigJsonStr(R"({"fitterEngineConfig":{"propagatorConfig":{"fitSampleSetConfig":{"fitSampleList":[]}}}})")
  );

  // Disabling defined plots:
  LogInfo << "Removing defined plots..." << std::endl;
  ConfigUtils::applyOverrides(
      cHandler.getConfig(),
      GenericToolbox::Json::readConfigJsonStr(R"({"fitterEngineConfig":{"propagatorConfig":{"plotGeneratorConfig":{}}}})")
  );

  // Defining signal samples
  JsonType xsecConfig{ ConfigUtils::readConfigFile( clParser.getOptionVal<std::string>("configFile") ) };
  cHandler.override( xsecConfig );
  LogInfo << "Override done." << std::endl;


  LogInfo << "Fetching propagator config into fitter config..." << std::endl;

  // it will handle all the deprecated config options and names properly
  FitterEngine fitter{nullptr};
  fitter.readConfig( GenericToolbox::Json::fetchValuePath<JsonType>( cHandler.getConfig(), "fitterEngineConfig" ) );

  DataSetManager& dataSetManager{fitter.getLikelihoodInterface().getDataSetManager()};

  // We are only interested in our MC. Data has already been used to get the post-fit error/values
  dataSetManager.getPropagator().setLoadAsimovData( true );

  // Disabling eigen decomposed parameters
  dataSetManager.getPropagator().setEnableEigenToOrigInPropagate( false );

  // Sample binning using parameterSetName
  for( auto& sample : dataSetManager.getPropagator().getSampleSet().getSampleList() ){

//    if( clParser.isOptionTriggered("usePreFit") ){
//      sample.setName( sample.getName() + " (pre-fit)" );
//    }

    // binning already set?
    if( not sample.getBinningFilePath().empty() ){ continue; }

    LogScopeIndent;
    LogInfo << sample.getName() << ": binning not set, looking for parSetBinning..." << std::endl;
    auto associatedParSet = GenericToolbox::Json::fetchValue(
        sample.getConfig(),
        {{"parSetBinning"}, {"parameterSetName"}},
        std::string()
    );

    LogThrowIf(associatedParSet.empty(), "Could not find parSetBinning.");

    // Looking for parSet
    auto foundDialCollection = std::find_if(
        dataSetManager.getPropagator().getDialCollectionList().begin(),
        dataSetManager.getPropagator().getDialCollectionList().end(),
        [&](const DialCollection& dialCollection_){
          auto* parSetPtr{dialCollection_.getSupervisedParameterSet()};
          if( parSetPtr == nullptr ){ return false; }
          return ( parSetPtr->getName() == associatedParSet );
        });
    LogThrowIf(
        foundDialCollection == dataSetManager.getPropagator().getDialCollectionList().end(),
        "Could not find " << associatedParSet << " among fit dial collections: "
                          << GenericToolbox::toString(dataSetManager.getPropagator().getDialCollectionList(),
                                                      [](const DialCollection& dialCollection_){
                                                        return dialCollection_.getTitle();
                                                      }
                          ));

    LogThrowIf(foundDialCollection->getDialBinSet().getBinList().empty(), "Could not find binning");
    sample.setBinningFilePath( foundDialCollection->getDialBinSet().getFilePath() );

  }

  // Load everything
  dataSetManager.initialize();

  Propagator& propagator{dataSetManager.getPropagator()};


  if( clParser.isOptionTriggered("dryRun") ){
    std::cout << cHandler.toString() << std::endl;

    LogAlert << "Exiting as dry-run is set." << std::endl;
    return EXIT_SUCCESS;
  }


  if( not clParser.isOptionTriggered("usePreFit") and fitterRootFile != nullptr ){

    // Load post-fit parameters as "prior" so we can reset the weight to this point when throwing toys
    LogWarning << std::endl << GenericToolbox::addUpDownBars("Injecting post-fit parameters...") << std::endl;
    ObjectReader::readObject<TNamed>( fitterRootFile.get(), "FitterEngine/postFit/parState_TNamed", [&](TNamed* parState_){
      propagator.getParametersManager().injectParameterValues( GenericToolbox::Json::readConfigJsonStr( parState_->GetTitle() ) );
      for( auto& parSet : propagator.getParametersManager().getParameterSetsList() ){
        if( not parSet.isEnabled() ){ continue; }
        for( auto& par : parSet.getParameterList() ){
          if( not par.isEnabled() ){ continue; }
          par.setPriorValue( par.getParameterValue() );
        }
      }
    });

    // Load the post-fit covariance matrix
    LogWarning << std::endl << GenericToolbox::addUpDownBars("Injecting post-fit covariance matrix...") << std::endl;
    ObjectReader::readObject<TH2D>(
        fitterRootFile.get(), "FitterEngine/postFit/Hesse/hessian/postfitCovarianceOriginal_TH2D",
        [&](TH2D* hCovPostFit_){
          propagator.getParametersManager().setGlobalCovarianceMatrix(std::make_shared<TMatrixD>(hCovPostFit_->GetNbinsX(), hCovPostFit_->GetNbinsX()));
          for( int iBin = 0 ; iBin < hCovPostFit_->GetNbinsX() ; iBin++ ){
            for( int jBin = 0 ; jBin < hCovPostFit_->GetNbinsX() ; jBin++ ){
              (*propagator.getParametersManager().getGlobalCovarianceMatrix())[iBin][jBin] = hCovPostFit_->GetBinContent(1 + iBin, 1 + jBin);
            }
          }
        }
    );
  }


  // Creating output file
  std::string outFilePath{};
  if( clParser.isOptionTriggered("outputFile") ){ outFilePath = clParser.getOptionVal<std::string>("outputFile"); }
  else{
    // appendixDict["optionName"] = "Appendix"
    // this list insure all appendices will appear in the same order
    std::vector<std::pair<std::string, std::string>> appendixDict{
        {"configFile", "%s"},
        {"fitterFile", "Fit_%s"},
        {"nToys", "nToys_%s"},
        {"randomSeed", "Seed_%s"},
        {"usePreFit", "PreFit"},
    };

    outFilePath = "xsecCalc_" + GundamUtils::generateFileName(clParser, appendixDict) + ".root";

    std::string outFolder{GenericToolbox::Json::fetchValue<std::string>(xsecConfig, "outputFolder", "./")};
    outFilePath = GenericToolbox::joinPath(outFolder, outFilePath);
  }

  app.setCmdLinePtr( &clParser );
  app.setConfigString( ConfigUtils::ConfigHandler{xsecConfig}.toString() );
  app.openOutputFile( outFilePath );
  app.writeAppInfo();

  auto* calcXsecDir{ GenericToolbox::mkdirTFile(app.getOutfilePtr(), "calcXsec") };
  bool useBestFitAsCentralValue{
    clParser.isOptionTriggered("useBfAsXsec")
    or GenericToolbox::Json::fetchValue<bool>(xsecConfig, "useBestFitAsCentralValue", false)
  };

  LogInfo << "Creating throws tree" << std::endl;
  auto* xsecThrowTree = new TTree("xsecThrow", "xsecThrow");
  xsecThrowTree->SetDirectory( GenericToolbox::mkdirTFile(calcXsecDir, "throws") ); // temp saves will be done here

  auto* xsecAtBestFitTree = new TTree("xsecAtBestFitTree", "xsecAtBestFitTree");
  xsecAtBestFitTree->SetDirectory( GenericToolbox::mkdirTFile(calcXsecDir, "throws") ); // temp saves will be done here

  LogInfo << "Creating normalizer objects..." << std::endl;
  // flux renorm with toys
  struct ParSetNormaliser{
    void readConfig(const JsonType& config_){
      LogScopeIndent;

      name = GenericToolbox::Json::fetchValue<std::string>(config_, "name");
      LogInfo << "ParSetNormaliser config \"" << name << "\": " << std::endl;

      // mandatory
      filePath = GenericToolbox::Json::fetchValue<std::string>(config_, "filePath");
      histogramPath = GenericToolbox::Json::fetchValue<std::string>(config_, "histogramPath");
      axisVariable = GenericToolbox::Json::fetchValue<std::string>(config_, "axisVariable");

      // optionals
      for( auto& parSelConfig : GenericToolbox::Json::fetchValue<JsonType>(config_, "parSelections") ){
        parSelections.emplace_back();
        parSelections.back().first = GenericToolbox::Json::fetchValue<std::string>(parSelConfig, "name");
        parSelections.back().second = GenericToolbox::Json::fetchValue<double>(parSelConfig, "value");
      }
      parSelections = GenericToolbox::Json::fetchValue(config_, "parSelections", parSelections);

      // init
      LogScopeIndent;
      LogInfo << GET_VAR_NAME_VALUE(filePath) << std::endl;
      LogInfo << GET_VAR_NAME_VALUE(histogramPath) << std::endl;
      LogInfo << GET_VAR_NAME_VALUE(axisVariable) << std::endl;

      if( not parSelections.empty() ){
        LogInfo << "parSelections:" << std::endl;
        for( auto& parSelection : parSelections ){
          LogScopeIndent;
          LogInfo << parSelection.first << " -> " << parSelection.second << std::endl;
        }
      }

    }
    void initialize(){
      LogThrowIf(dialCollectionPtr == nullptr, "Associated dial collection not provided.");
      LogThrowIf(not dialCollectionPtr->isBinned(), "Dial collection is not binned.");
      LogThrowIf(dialCollectionPtr->getSupervisedParameter() != nullptr, "Need a dial collection that handle a whole parSet.");

      file = std::make_shared<TFile>( filePath.c_str() );
      LogThrowIf(file == nullptr, "Could not open file");

      histogram = file->Get<TH1D>( histogramPath.c_str() );
      LogThrowIf(histogram == nullptr, "Could not find histogram.");
    }
    [[nodiscard]] double getNormFactor() const {
      double out{0};

      for( int iBin = 0 ; iBin < histogram->GetNbinsX() ; iBin++ ){
        double binValue{histogram->GetBinContent(1+iBin)};


        // do we skip this bin? if not, apply coefficient
        bool skipBin{true};
        for( size_t iParBin = 0 ; iParBin < dialCollectionPtr->getDialBinSet().getBinList().size() ; iParBin++ ){
          const DataBin& parBin = dialCollectionPtr->getDialBinSet().getBinList()[iParBin];

          bool isParBinValid{true};

          // first check the conditions
          for( auto& selection : parSelections ){
            if( parBin.isVariableSet(selection.first) and not parBin.isBetweenEdges(selection.first, selection.second) ){
              isParBinValid = false;
              break;
            }
          }

          // checking if the hist bin correspond to this
          if( parBin.isVariableSet(axisVariable) and not parBin.isBetweenEdges(axisVariable, histogram->GetBinCenter(1+iBin)) ){
            isParBinValid = false;
          }

          if( isParBinValid ){
            // ok, then apply the weight
            binValue *= dialCollectionPtr->getSupervisedParameterSet()->getParameterList()[iParBin].getParameterValue();

            skipBin = false;
            break;
          }
        }
        if( skipBin ){ continue; }

        // ok, add the fluctuated value
        out += binValue;
      }

      return out;
    }

    // config
    std::string name{};
    std::string filePath{};
    std::string histogramPath{};
    std::string axisVariable{};
    std::vector<std::pair<std::string, double>> parSelections{};

    // internals
    std::shared_ptr<TFile> file{nullptr};
    TH1D* histogram{nullptr};
    const DialCollection* dialCollectionPtr{nullptr}; // where the binning is defined
  };
  std::vector<ParSetNormaliser> parSetNormList;
  for( auto& parSet : propagator.getParametersManager().getParameterSetsList() ){
    if( GenericToolbox::Json::doKeyExist(parSet.getConfig(), "normalisations") ){
      for( auto& parSetNormConfig : GenericToolbox::Json::fetchValue<JsonType>(parSet.getConfig(), "normalisations") ){
        parSetNormList.emplace_back();
        parSetNormList.back().readConfig( parSetNormConfig );

        for( auto& dialCollection : propagator.getDialCollectionList() ){
          if( dialCollection.getSupervisedParameterSet() == &parSet ){
            parSetNormList.back().dialCollectionPtr = &dialCollection;
            break;
          }
        }

        parSetNormList.back().initialize();
      }
    }
  }



  // to be filled up
  struct BinNormaliser{
    void readConfig(const JsonType& config_){
      LogScopeIndent;

      name = GenericToolbox::Json::fetchValue<std::string>(config_, "name");

      if( not GenericToolbox::Json::fetchValue(config_, "isEnabled", bool(true)) ){
        LogWarning << "Skipping disabled re-normalization config \"" << name << "\"" << std::endl;
        return;
      }

      LogInfo << "Re-normalization config \"" << name << "\": ";

      if     ( GenericToolbox::Json::doKeyExist( config_, "meanValue" ) ){
        normParameter.first  = GenericToolbox::Json::fetchValue<double>(config_, "meanValue");
        normParameter.second = GenericToolbox::Json::fetchValue(config_, "stdDev", double(0.));
        LogInfo << "mean ± sigma = " << normParameter.first << " ± " << normParameter.second;
      }
      else if( GenericToolbox::Json::doKeyExist( config_, "disabledBinDim" ) ){
        disabledBinDim = GenericToolbox::Json::fetchValue<std::string>(config_, "disabledBinDim");
        LogInfo << "disabledBinDim = " << disabledBinDim;
      }
      else if( GenericToolbox::Json::doKeyExist( config_, "parSetNormName" ) ){
        parSetNormaliserName = GenericToolbox::Json::fetchValue<std::string>(config_, "parSetNormName");
        LogInfo << "parSetNormName = " << parSetNormaliserName;
      }
      else{
        LogInfo << std::endl;
        LogThrow("Unrecognized config.");
      }

      LogInfo << std::endl;
    }

    std::string name{};
    std::pair<double, double> normParameter{std::nan("mean unset"), std::nan("stddev unset")};
    std::string disabledBinDim{};
    std::string parSetNormaliserName{};

  };

  struct CrossSectionData{
    Sample* samplePtr{nullptr};
    JsonType config{};
    GenericToolbox::RawDataArray branchBinsData{};

    TH1D histogram{};
    std::vector<BinNormaliser> normList{};
  };
  std::vector<CrossSectionData> crossSectionDataList{};

  LogInfo << "Initializing xsec samples..." << std::endl;
  crossSectionDataList.reserve(propagator.getSampleSet().getSampleList().size() );
  for( auto& sample : propagator.getSampleSet().getSampleList() ){
    crossSectionDataList.emplace_back();
    auto& xsecEntry = crossSectionDataList.back();

    LogScopeIndent;
    LogInfo << "Defining xsec entry: " << sample.getName() << std::endl;
    xsecEntry.samplePtr = &sample;
    xsecEntry.config = sample.getConfig();
    xsecEntry.branchBinsData.resetCurrentByteOffset();
    std::vector<std::string> leafNameList{};
    leafNameList.reserve( sample.getMcContainer().getHistogram().nBins );
    for( int iBin = 0 ; iBin < sample.getMcContainer().getHistogram().nBins; iBin++ ){
      leafNameList.emplace_back(Form("bin_%i /D", iBin ));
      xsecEntry.branchBinsData.writeRawData( double(0) );
    }
    xsecEntry.branchBinsData.lockArraySize();

    xsecThrowTree->Branch(
        GenericToolbox::generateCleanBranchName( sample.getName() ).c_str(),
        xsecEntry.branchBinsData.getRawDataArray().data(),
        GenericToolbox::joinVectorString(leafNameList, ":").c_str()
    );
    xsecAtBestFitTree->Branch(
        GenericToolbox::generateCleanBranchName( sample.getName() ).c_str(),
        xsecEntry.branchBinsData.getRawDataArray().data(),
        GenericToolbox::joinVectorString(leafNameList, ":").c_str()
    );

    auto normConfigList = GenericToolbox::Json::fetchValue( xsecEntry.config, "normaliseParameterList", JsonType() );
    xsecEntry.normList.reserve( normConfigList.size() );
    for( auto& normConfig : normConfigList ){
      xsecEntry.normList.emplace_back();
      xsecEntry.normList.back().readConfig( normConfig );
    }

    xsecEntry.histogram = TH1D(
        sample.getName().c_str(),
        sample.getName().c_str(),
        sample.getMcContainer().getHistogram().nBins,
        0,
        sample.getMcContainer().getHistogram().nBins
    );
  }

  int nToys{ clParser.getOptionVal<int>("nToys") };

  // no bin volume of events -> use the current weight container
  for( auto& xsec : crossSectionDataList ){
    {
      auto& mcEvList{xsec.samplePtr->getMcContainer().getEventList()};
      std::for_each(mcEvList.begin(), mcEvList.end(), []( Event& ev_){ ev_.getWeights().current = 0; });
    }
    {
      auto& dataEvList{xsec.samplePtr->getDataContainer().getEventList()};
      std::for_each(dataEvList.begin(), dataEvList.end(), []( Event& ev_){ ev_.getWeights().current = 0; });
    }
  }

  bool enableEventMcThrow{true};
  bool enableStatThrowInToys{true};
  auto xsecCalcConfig   = GenericToolbox::Json::fetchValue( cHandler.getConfig(), "xsecCalcConfig", JsonType() );
  enableStatThrowInToys = GenericToolbox::Json::fetchValue( xsecCalcConfig, "enableStatThrowInToys", enableStatThrowInToys);
  enableEventMcThrow    = GenericToolbox::Json::fetchValue( xsecCalcConfig, "enableEventMcThrow", enableEventMcThrow);

  auto writeBinDataFct = std::function<void()>([&]{
    for( auto& xsec : crossSectionDataList ){

      xsec.branchBinsData.resetCurrentByteOffset();
      for( int iBin = 0 ; iBin < xsec.samplePtr->getMcContainer().getHistogram().nBins ; iBin++ ){
        double binData{ xsec.samplePtr->getMcContainer().getHistogram().binList[iBin].content };

        // special re-norm
        for( auto& normData : xsec.normList ){
          if( not std::isnan( normData.normParameter.first ) ){
            double norm{normData.normParameter.first};
            if( normData.normParameter.second != 0 ){ norm += normData.normParameter.second * gRandom->Gaus(); }
            binData /= norm;
          }
          else if( not normData.parSetNormaliserName.empty() ){
            ParSetNormaliser* parSetNormPtr{nullptr};
            for( auto& parSetNorm : parSetNormList ){
              if( parSetNorm.name == normData.parSetNormaliserName ){
                parSetNormPtr = &parSetNorm;
                break;
              }
            }
            LogThrowIf(parSetNormPtr == nullptr, "Could not find parSetNorm obj with name: " << normData.parSetNormaliserName);

            binData /= parSetNormPtr->getNormFactor();
          }
        }

        // no bin volume of events
        {
          auto& mcEvList{xsec.samplePtr->getMcContainer().getEventList()};
          std::for_each(mcEvList.begin(), mcEvList.end(), [&]( Event& ev_){
            if( iBin != ev_.getIndices().bin ){ return; }
            ev_.getWeights().current += binData;
          });
        }

        // set event weight
        {
          auto& dataEvList{xsec.samplePtr->getDataContainer().getEventList()};
          std::for_each(dataEvList.begin(), dataEvList.end(), [&]( Event& ev_){
            if( iBin != ev_.getIndices().bin ){ return; }
            ev_.getWeights().current = binData;
          });
        }

        // bin volume
        auto& bin = xsec.samplePtr->getBinning().getBinList()[iBin];
        double binVolume{1};

        for( auto& edges : bin.getEdgesList() ){
          if( edges.isConditionVar ){ continue; } // no volume, just a condition variable

          // is this bin excluded from the normalisation ?
          if( GenericToolbox::doesElementIsInVector(edges.varName, xsec.normList, [](const BinNormaliser& n){ return n.disabledBinDim; }) ){
            continue;
          }

          binVolume *= (edges.max - edges.min);
        }

        binData /= binVolume;
        xsec.branchBinsData.writeRawData( binData );
      }
    }
  });

  {
    LogWarning << "Calculating weight at best-fit" << std::endl;
    for( auto& parSet : propagator.getParametersManager().getParameterSetsList() ){ parSet.moveParametersToPrior(); }
    propagator.propagateParameters();
    writeBinDataFct();
    xsecAtBestFitTree->Fill();
    GenericToolbox::writeInTFile( GenericToolbox::mkdirTFile(calcXsecDir, "throws"), xsecAtBestFitTree );
  }


  //////////////////////////////////////
  // THROWS LOOP
  /////////////////////////////////////
  LogWarning << std::endl << GenericToolbox::addUpDownBars( "Generating toys..." ) << std::endl;

  std::stringstream ss; ss << LogWarning.getPrefixString() << "Generating " << nToys << " toys...";
  for( int iToy = 0 ; iToy < nToys ; iToy++ ){

    // loading...
    GenericToolbox::displayProgressBar( iToy+1, nToys, ss.str() );

    // Do the throwing:
    propagator.getParametersManager().throwParametersFromGlobalCovariance();
    propagator.propagateParameters();


    // disable stats throw
//    if( enableStatThrowInToys ){
//      for( auto& xsec : crossSectionDataList ){
//        if( enableEventMcThrow ){
//          // Take into account the finite amount of event in MC
//          xsec.samplePtr->getMcContainer().throwEventMcError();
//        }
//        // Asimov bin content -> toy data
//        xsec.samplePtr->getMcContainer().throwStatError();
//      }
//    }

    writeBinDataFct();

    // Write the branches
    xsecThrowTree->Fill();
  }


  LogInfo << "Writing throws..." << std::endl;
  GenericToolbox::writeInTFile( GenericToolbox::mkdirTFile(calcXsecDir, "throws"), xsecThrowTree );

  LogInfo << "Calculating mean & covariance matrix..." << std::endl;
  auto* meanValuesVector = GenericToolbox::generateMeanVectorOfTree(
      useBestFitAsCentralValue ? xsecAtBestFitTree : xsecThrowTree
  );
  auto* globalCovMatrix = GenericToolbox::generateCovarianceMatrixOfTree( xsecThrowTree );

  auto* globalCovMatrixHist = GenericToolbox::convertTMatrixDtoTH2D(globalCovMatrix);
  auto* globalCorMatrixHist = GenericToolbox::convertTMatrixDtoTH2D(GenericToolbox::convertToCorrelationMatrix(globalCovMatrix));

  std::vector<TH1D> binValues{};
  binValues.reserve(propagator.getSampleSet().getSampleList().size() );
  int iBinGlobal{-1};

  for( auto& xsec : crossSectionDataList ){

    for( int iBin = 0 ; iBin < xsec.samplePtr->getMcContainer().getHistogram().nBins ; iBin++ ){
      iBinGlobal++;

      std::string binTitle = xsec.samplePtr->getBinning().getBinList()[iBin].getSummary();
      double binVolume = xsec.samplePtr->getBinning().getBinList()[iBin].getVolume();

      xsec.histogram.SetBinContent( 1+iBin, (*meanValuesVector)[iBinGlobal] );
      xsec.histogram.SetBinError( 1+iBin, TMath::Sqrt( (*globalCovMatrix)[iBinGlobal][iBinGlobal] ) );
      xsec.histogram.GetXaxis()->SetBinLabel( 1+iBin, binTitle.c_str() );

      globalCovMatrixHist->GetXaxis()->SetBinLabel(1+iBinGlobal, GenericToolbox::joinPath(xsec.samplePtr->getName(), binTitle).c_str());
      globalCorMatrixHist->GetXaxis()->SetBinLabel(1+iBinGlobal, GenericToolbox::joinPath(xsec.samplePtr->getName(), binTitle).c_str());
      globalCovMatrixHist->GetYaxis()->SetBinLabel(1+iBinGlobal, GenericToolbox::joinPath(xsec.samplePtr->getName(), binTitle).c_str());
      globalCorMatrixHist->GetYaxis()->SetBinLabel(1+iBinGlobal, GenericToolbox::joinPath(xsec.samplePtr->getName(), binTitle).c_str());
    }

    xsec.histogram.SetMarkerStyle(kFullDotLarge);
    xsec.histogram.SetMarkerColor(kGreen-3);
    xsec.histogram.SetMarkerSize(0.5);
    xsec.histogram.SetLineWidth(2);
    xsec.histogram.SetLineColor(kGreen-3);
    xsec.histogram.SetDrawOption("E1");
    xsec.histogram.GetXaxis()->LabelsOption("v");
    xsec.histogram.GetXaxis()->SetLabelSize(0.02);
    xsec.histogram.GetYaxis()->SetTitle( GenericToolbox::Json::fetchValue(xsec.samplePtr->getConfig(), "yAxis", "#delta#sigma").c_str() );



    GenericToolbox::writeInTFile(
        GenericToolbox::mkdirTFile(calcXsecDir, "histograms"),
        &xsec.histogram, GenericToolbox::generateCleanBranchName( xsec.samplePtr->getName() )
    );

  }

  globalCovMatrixHist->GetXaxis()->SetLabelSize(0.02);
  globalCovMatrixHist->GetYaxis()->SetLabelSize(0.02);
  GenericToolbox::writeInTFile(GenericToolbox::mkdirTFile(calcXsecDir, "matrices"), globalCovMatrixHist, "covarianceMatrix");

  globalCorMatrixHist->GetXaxis()->SetLabelSize(0.02);
  globalCorMatrixHist->GetYaxis()->SetLabelSize(0.02);
  globalCorMatrixHist->GetZaxis()->SetRangeUser(-1, 1);
  GenericToolbox::writeInTFile(GenericToolbox::mkdirTFile(calcXsecDir, "matrices"), globalCorMatrixHist, "correlationMatrix");

  // now propagate to the engine for the plot generator
  LogInfo << "Re-normalizing the samples for the plot generator..." << std::endl;
  for( auto& xsec : crossSectionDataList ){
    // this gives the average as the event weights were summed together
    {
      auto &mcEvList{xsec.samplePtr->getMcContainer().getEventList()};
      std::vector<size_t> nEventInBin(xsec.histogram.GetNbinsX(), 0);
      for( size_t iBin = 0 ; iBin < nEventInBin.size() ; iBin++ ){
        nEventInBin[iBin] = std::count_if(mcEvList.begin(), mcEvList.end(), [iBin]( Event &ev_) {
          return ev_.getIndices().bin == iBin;
        });
      }

      std::for_each(mcEvList.begin(), mcEvList.end(), [&]( Event &ev_) {
        ev_.getWeights().current /= nToys;
        ev_.getWeights().current /= double(nEventInBin[ev_.getIndices().bin]);
      });
    }
    {
      auto &dataEvList{xsec.samplePtr->getDataContainer().getEventList()};
      std::vector<size_t> nEventInBin(xsec.histogram.GetNbinsX(), 0);
      for( size_t iBin = 0 ; iBin < nEventInBin.size() ; iBin++ ){
        nEventInBin[iBin] = std::count_if(dataEvList.begin(), dataEvList.end(), [iBin]( Event &ev_) {
          return ev_.getIndices().bin== iBin;
        });
      }

      std::for_each(dataEvList.begin(), dataEvList.end(), [&]( Event &ev_) {
        ev_.getWeights().current /= nToys;
        ev_.getWeights().current /= double(nEventInBin[ev_.getIndices().bin]);
      });
    }
  }

  LogInfo << "Generating xsec sample plots..." << std::endl;
  // manual trigger to tweak the error bars
  propagator.getPlotGenerator().generateSampleHistograms( GenericToolbox::mkdirTFile(calcXsecDir, "plots/histograms") );

  for( auto& histHolder : propagator.getPlotGenerator().getHistHolderList(0) ){
    if( not histHolder.isData ){ continue; } // only data will print errors

    const CrossSectionData* xsecDataPtr{nullptr};
    for( auto& xsecData : crossSectionDataList ){
      if( xsecData.samplePtr  == histHolder.samplePtr){
        xsecDataPtr = &xsecData;
        break;
      }
    }
    LogThrowIf(xsecDataPtr==nullptr, "corresponding data not found");

    // alright, now rescale error bars
    for( int iBin = 0 ; iBin < histHolder.histPtr->GetNbinsX() ; iBin++ ){
      // relative error should be set
      histHolder.histPtr->SetBinError(
          1+iBin,
          histHolder.histPtr->GetBinContent(1+iBin)
          * xsecDataPtr->histogram.GetBinError(1+iBin)
          / xsecDataPtr->histogram.GetBinContent(1+iBin)
      );
    }
  }

  //// SECTION TO MAKE THE HISTOGRAMS FROM THE DATA TTREE FOR THE CLOSURE TEST

  // here, fetch data from fitter file, to draw on top of MC predictions for each specific sample
  LogInfo << "Fetching data from fitter file..." << std::endl;
  int i{0};
  // Access the fitter output file
  LogInfo<<"Opening fitter output file: "<<fitterFile<<std::endl;
  std::unique_ptr<TFile> fitterFilePtr{ TFile::Open(fitterFile.c_str()) };

  // Define a closureVariable struct to store the data needed to generate the histograms
  struct closureVariable {
      Sample* samplePtr{nullptr};
      std::string varToPlot{};
      std::string variableFormula{};
      std::string binningFile{};
      TH1D* histogram{};
      bool rescaleAsBinWidth{false};
      TCanvas* canvas{};
  };

  /// PSEUDO CODE:
  //
  // for(sample : samples){
  //    for(varToPlot : varToPlotVector){
  //    closureVariable closureVar;
  //    closureVar.samplePtr = sample;
  //    closureVar: generate the histogram using the binning file defined in the plotGenerator
  //    closureVar.varToPlot = varToPlot;
  //    closureVar: connect varToPlot to a leafvar
  //    use the leafvar definition to fill the histogram properly
  //    }
  // }


  if( propagator.getPlotGenerator().getConfig().at("histogramsDefinition").is_array() ){
    std::cout << "\"Closure\" variable to plot: " << propagator.getPlotGenerator().getConfig().at("histogramsDefinition").size() <<std::endl;
  }


  std::string prePostFit ="";
  if( clParser.isOptionTriggered("usePreFit") ){
    prePostFit = "preFit";
  } else {
    prePostFit = "postFit";
  }

  std::vector<closureVariable> closureVarList;
  std::vector<std::string> varToPlotVector = propagator.getPlotGenerator().fetchListOfVarToPlot();
  size_t nHist = propagator.getPlotGenerator().getConfig().at("histogramsDefinition").size();
  for( auto& sample : propagator.getSampleSet().getSampleList() ) {

    LogInfo<<"Fetching data histogram for sample: "<<sample.getName()<<std::endl;
    TTree* dataTree = (TTree*)fitterFilePtr->Get( ("FitterEngine/"+prePostFit+"/events/"+sample.getName()+"/Data_TTree").std::string::c_str() ) ;
    LogErrorIf(dataTree == nullptr)<<"Could not find data tree for sample: "<<sample.getName()<<std::endl;
//     Save data tree in the output file (instead of the data tree that is just a copy of the mc tree)
//    GenericToolbox::writeInTFile(
//            GenericToolbox::mkdirTFile(calcXsecDir, "events"),
//            *dataTree,
//            GenericToolbox::generateCleanBranchName( (sample.getName()+"/Data_TTree").c_str() )
//    );
    for( size_t iHist = 0 ; iHist < nHist ; iHist++ ){ // this loop is over the variables to plot
      closureVariable closureVar;
      closureVar.samplePtr = &sample;
      closureVar.varToPlot = GenericToolbox::Json::fetchValue<std::string>(propagator.getPlotGenerator().getConfig().at("histogramsDefinition")[iHist], "varToPlot");
      closureVar.binningFile = GenericToolbox::Json::fetchValue<std::string>(propagator.getPlotGenerator().getConfig().at("histogramsDefinition")[iHist], "binningFile");
      closureVar.rescaleAsBinWidth = GenericToolbox::Json::fetchValue<bool>(propagator.getPlotGenerator().getConfig().at("histogramsDefinition")[iHist], "rescaleAsBinWidth");
      LogInfo<< "Variable: " << closureVar.varToPlot << " | Sample: " << closureVar.samplePtr->getName() << " | Binning file: " << closureVar.binningFile << std::endl;
      // Generate the histogram (using the binning file defined in the plotGenerator)
      std::vector<double> binEdges;
      readBinningFromFile(closureVar.binningFile.c_str(), binEdges);
      closureVar.histogram = new TH1D(
              (closureVar.samplePtr->getName() + "_" + closureVar.varToPlot).c_str(),
              (closureVar.samplePtr->getName() + " " + closureVar.varToPlot).c_str(),
              binEdges.size() - 1,
              &binEdges[0]
              );
      // load the formula
      for( int i = 0 ; i < propagator.getConfig()["dataSetList"][0]["mc"].at("overrideLeafDict").size() ; i++ ) {
        std::string eventVar = propagator.getConfig()["dataSetList"][0]["mc"].at("overrideLeafDict")[i].at("eventVar");
        if(eventVar == closureVar.varToPlot){
          closureVar.variableFormula = propagator.getConfig()["dataSetList"][0]["mc"].at("overrideLeafDict")[i].at("leafVar");
          break;
        }
      }
      // LogInfo<< "    Variable formula: " << closureVar.variableFormula << std::endl; // a bit verbose, do not print this
      LogErrorIf(closureVar.variableFormula.empty())<<"Could not find leafVar for varToPlot: "<<closureVar.varToPlot<<std::endl;
      // Fill the histogram
      closureVar.canvas = new TCanvas( ("canvas_"+closureVar.varToPlot+"_"+GenericToolbox::generateCleanBranchName(sample.getName()) ).c_str(), ("canvas_"+closureVar.varToPlot+"_"+sample.getName()).c_str(), 800, 600);
      closureVar.canvas->cd();
      dataTree->Draw( ("("+closureVar.variableFormula+")>>"+closureVar.histogram->GetName()).c_str() , "Event.eventWeight","hist");

      // rescale to bin width
      if(closureVar.rescaleAsBinWidth){
        for( int iBin = 0 ; iBin < closureVar.histogram->GetNbinsX() ; iBin++ ){
          double binWidth = closureVar.histogram->GetBinWidth(iBin+1);
          if (binWidth == 0) {
            LogError << "Bin " << iBin << " has a width of 0. Skipping rescaling." << std::endl;
            continue;
          }
          LogInfo<< "Bin " << iBin << " width: " << binWidth << " BinContent: "<< closureVar.histogram->GetBinContent(iBin+1) << std::endl;
          closureVar.histogram->SetBinContent(iBin+1, closureVar.histogram->GetBinContent(iBin+1)/binWidth);
          // closureVar.histogram->SetBinError(iBin+1, closureVar.histogram->GetBinError(iBin+1)/binWidth);
        }
      }
      // cosmetics
      closureVar.histogram->SetMarkerStyle(kFullDotLarge);
      closureVar.histogram->SetMarkerColor(kRed);
      closureVar.histogram->SetMarkerSize(0.5);
      closureVar.histogram->SetLineWidth(2);
      closureVar.histogram->SetLineColor(kRed);
      closureVar.histogram->SetDrawOption("hist");
      closureVar.histogram->SetTitle( (closureVar.varToPlot+" for "+closureVar.samplePtr->getName()  ).c_str() );
      closureVar.histogram->GetXaxis()->SetTitle(closureVar.varToPlot.c_str());
      GenericToolbox::writeInTFile(
              GenericToolbox::mkdirTFile(calcXsecDir, "plots/data_histograms"),
              closureVar.histogram,
              GenericToolbox::generateCleanBranchName( "data_" + closureVar.varToPlot + "_" + sample.getName()  )
      );
      // Now generate, for each sample, a canvas with all the histograms: stack of all the MC with different reaction
      // codes generated before, and the data histogram just generated from the fitter output file

      closureVarList.push_back(closureVar);
    }

  }

  LogInfo << "Generating canvases " << std::endl;
  propagator.getPlotGenerator().generateCanvas(
          propagator.getPlotGenerator().getHistHolderList(),
          GenericToolbox::mkdirTFile(calcXsecDir, "plots/canvas")
  );


  // overlay the data histograms on the MC histograms
  for(auto closureVar : closureVarList) {
    std::string cleanSampleName = GenericToolbox::generateCleanBranchName(closureVar.samplePtr->getName());
    TCanvas * c_MC = (TCanvas*)app.getOutfilePtr()->Get( ("calcXsec/plots/canvas/"+closureVar.varToPlot+"/ReactionCode/sample_"+cleanSampleName+"_TCanvas").c_str() );
    if(!c_MC){
      LogError << "Could not find canvas for variable: " << closureVar.varToPlot << " and sample: " << cleanSampleName << std::endl;
      continue;
    }else{
      // how many pads in this canvas?
      int nPads = c_MC->GetListOfPrimitives()->GetSize();
      c_MC->cd(0);
      gPad->GetListOfPrimitives()->Print();
      closureVar.histogram->Draw("hist same");
    }
    c_MC->Update();
    GenericToolbox::writeInTFile(
            GenericToolbox::mkdirTFile(calcXsecDir, "plots/canvas"),
            c_MC,
            GenericToolbox::generateCleanBranchName( "Closure_"+closureVar.varToPlot+"_"+cleanSampleName+"_TCanvas" )
    );
  }


  LogInfo << "Writing event samples in TTrees..." << std::endl;
  dataSetManager.getTreeWriter().writeSamples(
      GenericToolbox::mkdirTFile(calcXsecDir, "events"),
      dataSetManager.getPropagator()
  );

} // end of main





void readBinningFromFile(const char* filename, std::vector<Double_t>& binEdges) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error opening file: " << filename << std::endl;
    return;
  }

  std::string line;
  // Skip the first line
  std::getline(file, line);
  double lastRightEdge;

  while (std::getline(file, line)) {
    Double_t lower, upper;
    std::istringstream iss(line);
    // skip empty lines
    if( line.empty() ){ continue; }
    // skip commented lines
    if( line[0] == '#' ){ continue; }
    if (!(iss >> lower >> upper)) {
      LogError << "Error reading bin edges from file: " << filename << std::endl;
      break;
    }
    binEdges.push_back(lower);
    lastRightEdge = upper;
  }

  // Add the last upper edge
  binEdges.push_back(lastRightEdge);

  file.close();
}