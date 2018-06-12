/*
Copyright (c) 2016,
Dan Bethell, Johannes Saam, Vahan Sosoyan, Brian Scherbinski.
All rights reserved. See COPYING.txt for more details.
*/

#ifndef ATON_CLIENT_H_
#define ATON_CLIENT_H_

#include "Data.h"
#include <boost/asio.hpp>

// Used to send an image to a Server
// The Client class is created each time an application wants to send
// an image to the Server. Once it is instantiated the application should
// call openImage(), send(), and closeImage() to send an image to the Server
class Client
{
friend class Server;
public:
    // Creates a new Client object and tell it to connect any messages to
    // the specified host/port
    Client(std::string hostname, int port);

    ~Client();

    // Sends a message to the Server to open a new image
    // The header parameter is used to tell the Server the size of image
    // buffer to allocate.
    void openImage(DataHeader& header);
    
    // Sends a section of image data to the Server
    // Once an image is open a Client can use this to send a series of
    // pixel blocks to the Server. The Data object passed must correctly
    // specify the block position and dimensions as well as provide a
    // pointer to pixel data.
    void sendPixels(DataPixels& data);

    // Sends a message to the Server that the Clients has finished
    // This tells the Server that a Client has finished sending pixel
    // information for an image.
    void closeImage();
    
private:
    void connect(std::string host, int port);
    void disconnect();
    void quit();

    // Store the port we should connect to
    std::string mHost;
    int mPort, mImageId;
    bool mIsConnected;

    // TCP stuff
    boost::asio::io_service mIoService;
    boost::asio::ip::tcp::socket mSocket;
};

#endif // ATON_CLIENT_H_
