#include "document.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);if(argc!=2 || VIPS_INIT(argv[0]))return 2;
 vips_concurrency_set(4);vips_cache_set_max(0);vips_cache_set_max_mem(128ULL*1024*1024);
 int result=0;
 try{
 QTemporaryDir tmp(QString::fromLocal8Bit(argv[1])+"/large-test-XXXXXX");
 if(!tmp.isValid())throw std::runtime_error("temp directory");
 qputenv("TMPDIR",tmp.path().toUtf8());qputenv("VIPS_DISC_THRESHOLD","64m");
 auto color=vips::VImage::black(30000,24000,vips::VImage::option()->set("bands",3)).new_from_image({210,100,30}).copy(vips::VImage::option()->set("interpretation",VIPS_INTERPRETATION_sRGB));
 auto path=tmp.path()+"/large.tif";QElapsedTimer clock;clock.start();
 color.tiffsave(path.toUtf8().constData(),vips::VImage::option()->set("tile",true)->set("bigtiff",true)->set("compression",VIPS_FOREIGN_TIFF_COMPRESSION_NONE));
 std::cout<<"TIFF bytes="<<QFileInfo(path).size()<<" generated_ms="<<clock.restart()<<std::endl;
 if(QFileInfo(path).size()<2000000000LL)throw std::runtime_error("TIFF too small");
 Document doc;auto cancel=std::make_shared<std::atomic_bool>(false);
 auto info=doc.open(path,{},cancel);auto tile=doc.tile(0,58,46);
 std::cout<<"tile="<<tile.image.width()<<"x"<<tile.image.height()<<" RGB="<<tile.image.pixelColor(10,10).red()<<","<<tile.image.pixelColor(10,10).green()<<","<<tile.image.pixelColor(10,10).blue()<<std::endl;
 if(info.size!=QSize(30000,24000) || tile.image.pixelColor(10,10).red()!=210)throw std::runtime_error("large TIFF mismatch");
 std::cout<<"TIFF open + 1:1 tile ms="<<clock.restart()<<std::endl;
 auto overview=doc.tile(6,0,0);if(overview.image.isNull())throw std::runtime_error("overview");
 std::cout<<"TIFF overview ms="<<clock.restart()<<std::endl;doc.clear();
 auto noise=[](int seed){return vips::VImage::gaussnoise(8000,8000,vips::VImage::option()->set("mean",128.0)->set("sigma",50.0)->set("seed",seed));};
 auto rgb=noise(1).bandjoin(noise(2)).bandjoin(noise(3)).cast(VIPS_FORMAT_UCHAR).copy(vips::VImage::option()->set("interpretation",VIPS_INTERPRETATION_sRGB));
 auto jpg=tmp.path()+"/large.jpg";rgb.jpegsave(jpg.toUtf8().constData(),vips::VImage::option()->set("Q",100)->set("subsample_mode",VIPS_FOREIGN_SUBSAMPLE_OFF));
 std::cout<<"JPEG bytes="<<QFileInfo(jpg).size()<<" generated_ms="<<clock.restart()<<std::endl;
 if(QFileInfo(jpg).size()<200000000LL)throw std::runtime_error("JPEG too small");
 info=doc.open(jpg,{},cancel);tile=doc.tile(4,0,0);
 if(info.size!=QSize(8000,8000) || tile.image.isNull())throw std::runtime_error("large JPEG mismatch");
 std::cout<<"JPEG open + overview ms="<<clock.restart()<<std::endl;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}
 vips_shutdown();return result;
}
