/*
Copyright (c) 2016,
Dan Bethell, Johannes Saam, Vahan Sosoyan, Brian Scherbinski.
All rights reserved. See COPYING.txt for more details.
*/

#include <ai.h>
#include "Data.h"
#include "Client.h"

using boost::asio::ip::tcp;

AI_DRIVER_NODE_EXPORT_METHODS(AtonDriverMtd);

const char* getHost()
{
    const char* def_host = getenv("ATON_HOST");
    
    if (def_host == NULL)
        def_host = "127.0.0.1";

    char* aton_host = new char[strlen(def_host) + 1];
    strcpy(aton_host, def_host);

    return aton_host;
}

const int getPort()
{
    const char* def_port = getenv("ATON_PORT");
    int aton_port;
    
    if (def_port == NULL)
        aton_port = 9201;
    else
        aton_port = atoi(def_port);
    
    return aton_port;
}

struct ShaderData
{
    Client* client;
    int xres, yres, min_x, min_y, max_x, max_y, index;
};

node_parameters
{
    const char* host  = getHost();
    const int port = getPort();
    
    AiParameterStr("host", host);
    AiParameterInt("port", port);
    AiParameterInt("index", 0);
    AiParameterStr("output", "");
    AiParameterStr("comment", "");
    
    delete host;
    
#ifdef ARNOLD_5
    AiMetaDataSetStr(nentry, NULL, "maya.translator", "aton");
    AiMetaDataSetStr(nentry, NULL, "maya.attr_prefix", "");
    AiMetaDataSetBool(nentry, NULL, "display_driver", true);
    AiMetaDataSetBool(nentry, NULL, "single_layer_driver", false);
#else
    AiMetaDataSetStr(mds, NULL, "maya.translator", "aton");
    AiMetaDataSetStr(mds, NULL, "maya.attr_prefix", "");
    AiMetaDataSetBool(mds, NULL, "display_driver", true);
    AiMetaDataSetBool(mds, NULL, "single_layer_driver", false);
#endif
    
}

node_initialize
{
    ShaderData* data = (ShaderData*)AiMalloc(sizeof(ShaderData));
    data->client = NULL;

#ifdef ARNOLD_5
    AiDriverInitialize(node, true);
    AiNodeSetLocalData(node, data);
#else
    AiDriverInitialize(node, true, data);
#endif
    
}

node_update {}

driver_supports_pixel_type { return true; }

driver_extension { return NULL; }

driver_open
{
    // Construct full version number
    char arch[3], major[3], minor[3], fix[3];
    AiGetVersion(arch, major, minor, fix);
    
    const int version = atoi(arch) * 1000000 +
                        atoi(major) * 10000 +
                        atoi(minor) * 100 +
                        atoi(fix);
    
#ifdef ARNOLD_5
    ShaderData* data = (ShaderData*)AiNodeGetLocalData(node);
#else
    ShaderData* data = (ShaderData*)AiDriverGetLocalData(node);
#endif
    
    // Get Frame number
    AtNode* options = AiUniverseGetOptions();
    const float currentFrame = AiNodeGetFlt(options, "frame");
    
    // Get Host and Port
    const char* host = AiNodeGetStr(node, "host");    
    const int port = AiNodeGetInt(node, "port");
    
    // Get Camera Matrix
    AtNode* camera = (AtNode*)AiNodeGetPtr(options, "camera");
    
#ifdef ARNOLD_5
    const AtMatrix& cMat = AiNodeGetMatrix(camera, "matrix");
#else
    AtMatrix cMat;
    AiNodeGetMatrix(camera, "matrix", cMat);
#endif
    
    const float cam_matrix[16] = {cMat[0][0], cMat[1][0], cMat[2][0], cMat[3][0],
                                  cMat[0][1], cMat[1][1], cMat[2][1], cMat[3][1],
                                  cMat[0][2], cMat[1][2], cMat[2][2], cMat[3][2],
                                  cMat[0][3], cMat[1][3], cMat[2][3], cMat[3][3]};

    // Get Camera Field of view
    const float cam_fov = AiNodeGetFlt(camera, "fov");

    // Get Resolution
    const int xres = AiNodeGetInt(options, "xres");
    const int yres = AiNodeGetInt(options, "yres");
    
    // Get Regions
    const int min_x = AiNodeGetInt(options, "region_min_x");
    const int min_y = AiNodeGetInt(options, "region_min_y");
    const int max_x = AiNodeGetInt(options, "region_max_x");
    const int max_y = AiNodeGetInt(options, "region_max_y");
    
    // Setting Origin
    data->min_x = (min_x == INT_MIN) ? 0 : min_x;
    data->min_y = (min_y == INT_MIN) ? 0 : min_y;
    data->max_x = (max_x == INT_MIN) ? 0 : max_x;
    data->max_y = (max_y == INT_MIN) ? 0 : max_y;
   
    // Setting X Resolution
    if (data->min_x < 0 && data->max_x >= xres)
        data->xres = data->max_x - data->min_x + 1;
    else if (data->min_x >= 0 && data->max_x < xres)
        data->xres = xres;
    else if (data->min_x < 0 && data->max_x < xres)
        data->xres = xres - min_x;
    else if(data->min_x >= 0 && data->max_x >= xres)
        data->xres = xres + (max_x - xres + 1);
    
    // Setting Y Resolution
    if (data->min_y < 0 && data->max_y >= yres)
        data->yres = data->max_y - data->min_y + 1;
    else if (data->min_y >= 0 && data->max_y < yres)
        data->yres = yres;
    else if (data->min_y < 0 && data->max_y < yres)
        data->yres = yres - min_y;
    else if(data->min_y >= 0 && data->max_y >= yres)
        data->yres = yres + (max_y - yres + 1);
    
    // Get area of region
    const long long rArea = data->xres * data->yres;
    
    // Make image header & send to server
    DataHeader dh(data->xres,
                  data->yres,
                  rArea,
                  version,
                  currentFrame,
                  cam_fov,
                  cam_matrix);

    try // Now we can connect to the server and start rendering
    {
        if (data->client == NULL)
        {
            boost::system::error_code ec;
            boost::asio::ip::address::from_string(host, ec);
            if (!ec)
                data->client = new Client(host, port);
        }
        data->client->openImage(dh);
    }
    catch(const std::exception &e)
    {
        const char* err = e.what();
        AiMsgError("ATON | %s", err);
    }
}

driver_needs_bucket { return true; }

driver_prepare_bucket {}

driver_process_bucket {}

driver_write_bucket
{
    
#ifdef ARNOLD_5
    ShaderData* data = (ShaderData*)AiNodeGetLocalData(node);
#else
    ShaderData* data = (ShaderData*)AiDriverGetLocalData(node);
#endif

    int pixel_type;
    int spp = 0;
    const void* bucket_data;
    const char* aov_name;
    
    if (data->min_x < 0)
        bucket_xo = bucket_xo - data->min_x;
    if (data->min_y < 0)
        bucket_yo = bucket_yo - data->min_y;
    
    while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, &bucket_data))
    {
        const float* ptr = reinterpret_cast<const float*>(bucket_data);
        const long long ram = AiMsgUtilGetUsedMemory();
        const unsigned int time = AiMsgUtilGetElapsedTime();

        switch (pixel_type)
        {
            case(AI_TYPE_INT):
            case(AI_TYPE_UINT):
            case(AI_TYPE_FLOAT):
                spp = 1;
                break;
            case(AI_TYPE_RGBA):
                spp = 4;
                break;
            default:
                spp = 3;
        }
        
        // Create our DataPixels object
        DataPixels dp(data->xres,
                      data->yres,
                      bucket_xo,
                      bucket_yo,
                      bucket_size_x,
                      bucket_size_y,
                      spp, ram, time, aov_name, ptr);

        // Send it to the server
        data->client->sendPixels(dp);
    }
}

driver_close {}

node_finish
{
    AiMsgInfo("[Aton] driver finish");
   
    // Release the driver
#ifdef ARNOLD_5
    ShaderData* data = (ShaderData*)AiNodeGetLocalData(node);
#else
    ShaderData* data = (ShaderData*)AiDriverGetLocalData(node);
#endif
    data->client->closeImage();
    delete data->client;
    AiFree(data);

#ifndef ARNOLD_5
    AiDriverDestroy(node);
#endif
}

node_loader
{
    sprintf(node->version, AI_VERSION);

    switch (i)
    {
        case 0:
            node->methods = (AtNodeMethods*) AtonDriverMtd;
            node->output_type = AI_TYPE_RGBA;
            node->name = "driver_aton";
            node->node_type = AI_NODE_DRIVER;
            break;
        default:
        return false;
    }
    return true;
}
