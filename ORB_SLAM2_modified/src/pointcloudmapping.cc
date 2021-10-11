/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2016  <copyright holder> <email>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "pointcloudmapping.h"
#include <KeyFrame.h>
#include <opencv2/highgui/highgui.hpp>
#include <pcl/visualization/cloud_viewer.h>
#include "Converter.h"
#include <pcl/io/pcd_io.h>                  //SKS: this is added for saving map
#include "iostream"
#include "fstream"
#include <string>
#include <sstream>
#include <vector>
#include <iterator>

#include<ctime>

using std::cout; using std::cin;
using std::endl; using std::string;
using std::vector;
using std::istringstream;

PointCloudMapping::PointCloudMapping(double resolution_)
{
    this->resolution = resolution_;
    voxel.setLeafSize( resolution, resolution, resolution);
    globalMap = boost::make_shared< PointCloud >( );

    viewerThread = make_shared<thread>( bind(&PointCloudMapping::viewer, this ) );
}

void PointCloudMapping::shutdown()
{
    {
        unique_lock<mutex> lck(shutDownMutex);
        shutDownFlag = true;
        keyFrameUpdated.notify_one();
    }
    viewerThread->join();
}

void PointCloudMapping::insertKeyFrame(KeyFrame* kf, cv::Mat& color, cv::Mat& depth)
{
    cout<<"receive a keyframe, id = "<<kf->mnId<<endl;
    unique_lock<mutex> lck(keyframeMutex);
    keyframes.push_back( kf );
    colorImgs.push_back( color.clone() );
    depthImgs.push_back( depth.clone() );

    keyFrameUpdated.notify_one();
}

pcl::PointCloud< PointCloudMapping::PointT >::Ptr PointCloudMapping::generatePointCloud(KeyFrame* kf, cv::Mat& color, cv::Mat& depth)
{
    PointCloud::Ptr tmp( new PointCloud() );
    std::ofstream fout("kfinfo.txt",std::ios_base::out | std::ios_base::app);
    long double ts;
    char line[100];
    string rgbname;
    string depthname;

    vector<string> x;
    x.clear();

    vector<string> words{};

    // point cloud is null ptr
    for ( int m=0; m<depth.rows; m+=3 )
    {
        for ( int n=0; n<depth.cols; n+=3 )
        {
            float d = depth.ptr<float>(m)[n];
            if (d < 0.01 || d>10)
                continue;
            PointT p;
            p.z = d;
            p.x = ( n - kf->cx) * p.z / kf->fx;
            p.y = ( m - kf->cy) * p.z / kf->fy;

            p.b = color.ptr<uchar>(m)[n*3];
            p.g = color.ptr<uchar>(m)[n*3+1];
            p.r = color.ptr<uchar>(m)[n*3+2];

           cout<<"value of p "<<p.z<<" "<<p.x<<" "<<p.y<<" "<<p.b<<" "<<p.g<<" "<<p.r<<" "<<endl;

/*
value of p 0.3716 -0.15426 0.0822123     
value of p 0.345 -0.141518 0.0763273    
value of p 0.3432 -0.139089 0.0759291 ! $  
value of p 0.3422 -0.136998 0.0757079 ' ! 
                                           
value of p 0.3392 -0.134126 0.0750442 *  	 
value of p 0.3384 -0.132143 0.0748672 +  
 
value of p 0.3374 -0.13009 0.0746459 +  
 
value of p 0.3354 -0.127666 0.0742035 ,  
value of p 0.3336 -0.125338 0.0738052 -  	 
value of p 0.3328 -0.123398 0.0736282 .  
                                           
value of p 0.3318 -0.121392 0.073407 -  	 
value of p 0.3318 -0.119758 0.073407 .   

*/
            tmp->points.push_back(p);
        }
    }
    ts = kf->mTimeStamp;
    cout<<"kf time stamp "<<kf->mTimeStamp<<endl;

    fout<< ts<<'\n';
    for(int i=0; i<kf->GetPose().rows; i++)
    {
        for(int j=0; j<kf->GetPose().cols; j++)
        {
            fout<<kf->GetPose().at<float>(i,j)<<" ";
            cout<<kf->GetPose().at<float>(i,j)<<"\t";
        }
        fout<<endl;
    }
    fout.close();

    cout<<"get pose "<<kf->GetPose()<<endl;
    Eigen::Isometry3d T = ORB_SLAM2::Converter::toSE3Quat( kf->GetPose() );
    PointCloud::Ptr cloud(new PointCloud);
    pcl::transformPointCloud( *tmp, *cloud, T.inverse().matrix());
    cloud->is_dense = false;

    cout<<"generate point cloud for kf "<<kf->mnId<<", size="<<cloud->points.size()<<endl;
    return cloud;
}


void PointCloudMapping::viewer()
{
    pcl::visualization::CloudViewer viewer("viewer");
    while(1)
    {
        {
            unique_lock<mutex> lck_shutdown( shutDownMutex );
            if (shutDownFlag)
            {
                break;
            }
        }
        {
            unique_lock<mutex> lck_keyframeUpdated( keyFrameUpdateMutex );
            keyFrameUpdated.wait( lck_keyframeUpdated );
        }

        // keyframe is updated
        size_t N=0;
        {
            unique_lock<mutex> lck( keyframeMutex );
            N = keyframes.size();
        }

        for ( size_t i=lastKeyframeSize; i<N ; i++ )
        {
            PointCloud::Ptr p = generatePointCloud( keyframes[i], colorImgs[i], depthImgs[i] );
            *globalMap += *p;
        }
//        PointCloud::Ptr tmp(new PointCloud());
//        voxel.setInputCloud( globalMap );
//        voxel.filter( *tmp );
//        globalMap->swap( *tmp );
        viewer.showCloud( globalMap );
        cout<<"show global map, size="<<globalMap->points.size()<<endl;
        lastKeyframeSize = N;
    }
    std::time_t rawtime;
    std::tm* timeinfo;
    char buffer [80];

    std::time(&rawtime);
    timeinfo = std::localtime(&rawtime);

    std::strftime(buffer,80,"%Y-%m-%d-%H-%M-%S",timeinfo);

    string str = buffer;
    str= "result/"+str+ ".pcd";

    pcl::io::savePCDFileASCII (str, *globalMap);
    cout<<"SKS: viewer end__ saving map as => "+str <<endl;
}