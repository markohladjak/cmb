#ifndef IFILEUPLOAD_H
#define IFILEUPLOAD_H
#pragma once

#include "stddef.h"
#include "stdint.h"

class IFileUploadHandler
{
public:
    virtual void upload_handler(int id, size_t size, size_t data_size, uint8_t *data) = 0;
};

class IFileUpload
{
public:
	typedef void (IFileUploadHandler::*UploadHandler_t)(int id, size_t size, size_t data_size, uint8_t *data);

	virtual void RegisterUploadHandler(IFileUploadHandler *obj, UploadHandler_t handler) = 0;
};


#endif