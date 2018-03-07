#include "Visualizer.h"
#include "QApplication"
#include <QFileInfo>
#include <QDir>
#include <GSLAM/core/Dataset.h>
#include <GSLAM/core/Timer.h>
#include <RTMapper.h>

class System: public GSLAM::GObjectHandle
{
public:
    System(){
        _vis=Visualizer::create(NULL);
        if(_vis) _vis->show();
        if(!svar.exist("Map2DFusion.CacheFolder"))
            svar.insert("Map2DFusion.CacheFolder","cache");
        QString cacheFolder=svar.GetString("Map2DFusion.CacheFolder","cache").c_str();
        DelDir(cacheFolder);
        QDir(cacheFolder).mkdir(cacheFolder);
    }

    bool DelDir(const QString &path)
    {
        if (path.isEmpty()){
            return false;
        }
        QDir dir(path);
        if(!dir.exists()){
            return true;
        }
        dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
        QFileInfoList fileList = dir.entryInfoList();
        foreach (QFileInfo file, fileList){
            if (file.isFile()){
                file.dir().remove(file.fileName());
            }else{
                DelDir(file.absoluteFilePath());
            }
        }
        return dir.rmpath(dir.absolutePath());
    }

    void run(){
        std::string dataset=svar.GetString("Dataset","");
        if(dataset.empty()){
            LOG(ERROR)<<"Please set dataset with Dataset=dataset_path";
            return;
        }
        if(!_dataset.open(dataset))
        {
            LOG(ERROR)<<"Failed to open Dataset "<<svar.GetString("Dataset","");
            return ;
        }
#ifdef RTMAPPER_PLUGIN
        RTMapperPtr rtmapper=RTMapper::create(svar.GetString("RTMapper","libRTMapperSDK"));
#else
        RTMapperPtr rtmapper=createRTMapperInstance();
#endif
        if(!rtmapper) {
            LOG(ERROR)<<"Failed to load RTMapperSDK.";
            return;
        }
        else LOG(INFO)<<"Loaded RTMapperSDK version "<<rtmapper->version();

        rtmapper->setCallback(this);
        rtmapper->setSvar(svar);

        GSLAM::Rate rate(100);
        bool        isFirstFrame=true;
        int&        shouldStop=svar.i["ShouldStop"];
        while(!shouldStop){
            rate.sleep();
            GSLAM::FramePtr frame=_dataset.grabFrame();
            if(!frame) {
                LOG(INFO)<<"Dataset processed.";
                break;
            }
            if(frame->getImage().empty()) {
                LOG(ERROR)<<"Frame has no image.";
                break;
            }
            if(!frame->getCamera().isValid())
            {
                LOG(ERROR)<<"Camera not valid.";
                break;
            }
            if(isFirstFrame){
                isFirstFrame=false;
                GSLAM::Point3d lla;
                if(!frame->getGPSLLA(lla)) {
                    LOG(ERROR)<<"No GPS information.";
                    break;
                }
                if(_vis)
                {
                    _vis->setHomeGPSPosition(lla);
                    _vis->goHome();
                }

            }
            rtmapper->track(frame);
        }


        if(_tileManager) if(!_tileManager->save("ortho.tif")) LOG(ERROR)<<"Failed to export tif";
        if(_map)         if(!_map->save("sparse.ply")) LOG(ERROR)<<"Failed to export ply.";
    }


    virtual void handle(const SPtr<GSLAM::GObject>& obj){
        if(auto tileManager=std::dynamic_pointer_cast<GSLAM::TileManager>(obj)){
            _tileManager=tileManager;
            if(_vis) _vis->showTileManager(_tileManager);
        }
        else if(auto map=std::dynamic_pointer_cast<GSLAM::Map>(obj))
        {
            _map=map;
            LOG(INFO)<<"Got map "<<_map->type()<<" with "<<_map->frameNum()<<" frames.";
        }
    }

    GSLAM::Dataset          _dataset;
    SPtr<Visualizer>        _vis;
    GSLAM::TileManagerPtr   _tileManager;
    GSLAM::MapPtr           _map;
};

int main(int argc,char** argv)
{
    svar.ParseMain(argc,argv);
    QApplication app(argc,argv);

    System sys;
    std::thread processThread(&System::run,&sys);

    int ret=app.exec();
    processThread.join();
    return ret;
}


