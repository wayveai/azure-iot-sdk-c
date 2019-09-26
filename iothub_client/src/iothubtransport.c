// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <signal.h>
#include <stddef.h>
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "internal/iothubtransport.h"
#include "iothub_client_core.h"
#include "internal/iothub_client_private.h"
#include "azure_c_shared_utility/lock.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/vector.h"

#include "internal/iothubtransport.h"
#include "internal/iothub_client_private.h"
#include "iothub_transport_ll.h"
#include "iothub_client_core.h"

typedef struct TRANSPORT_HANDLE_DATA_TAG
{
    TRANSPORT_LL_HANDLE transportLLHandle;
    LOCK_HANDLE lockHandle;
    TRANSPORT_PROVIDER_FIELDS;
} TRANSPORT_HANDLE_DATA;

TRANSPORT_HANDLE IoTHubTransport_Create(IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol, const char* iotHubName, const char* iotHubSuffix)
{
    TRANSPORT_HANDLE_DATA *result;
    TRANSPORT_CALLBACKS_INFO transport_cb;

    if (protocol == NULL || iotHubName == NULL || iotHubSuffix == NULL)
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_002: [ If protocol is NULL, this function shall return NULL. ]*/
        /*Codes_SRS_IOTHUBTRANSPORT_17_003: [ If iotHubName is NULL, this function shall return NULL. ]*/
        /*Codes_SRS_IOTHUBTRANSPORT_17_004: [ If iotHubSuffix is NULL, this function shall return NULL. ]*/
        LogError("Invalid NULL argument, protocol [%p], name [%p], suffix [%p].", protocol, iotHubName, iotHubSuffix);
        result = NULL;
    }
    else if (IoTHubClientCore_LL_GetTransportCallbacks(&transport_cb) != 0)
    {
        LogError("Failure getting transport callbacks");
        result = NULL;
    }
    else
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_032: [ IoTHubTransport_Create shall allocate memory for the transport data. ]*/
        result = (TRANSPORT_HANDLE_DATA*)malloc(sizeof(TRANSPORT_HANDLE_DATA));
        if (result == NULL)
        {
            /*Codes_SRS_IOTHUBTRANSPORT_17_040: [ If memory allocation fails, IoTHubTransport_Create shall return NULL. ]*/
            LogError("Transport handle was not allocated.");
        }
        else
        {
            TRANSPORT_PROVIDER * transportProtocol = (TRANSPORT_PROVIDER*)(protocol());
            IOTHUB_CLIENT_CONFIG upperConfig;
            upperConfig.deviceId = NULL;
            upperConfig.deviceKey = NULL;
            upperConfig.iotHubName = iotHubName;
            upperConfig.iotHubSuffix = iotHubSuffix;
            upperConfig.protocol = protocol;
            upperConfig.protocolGatewayHostName = NULL;

            IOTHUBTRANSPORT_CONFIG transportLLConfig;
            memset(&transportLLConfig, 0, sizeof(IOTHUBTRANSPORT_CONFIG));
            transportLLConfig.upperConfig = &upperConfig;
            transportLLConfig.waitingToSend = NULL;

            /*Codes_SRS_IOTHUBTRANSPORT_17_005: [ IoTHubTransport_Create shall create the lower layer transport by calling the protocol's IoTHubTransport_Create function. ]*/
            result->transportLLHandle = transportProtocol->IoTHubTransport_Create(&transportLLConfig, &transport_cb, NULL);
            if (result->transportLLHandle == NULL)
            {
                /*Codes_SRS_IOTHUBTRANSPORT_17_006: [ If the creation of the transport fails, IoTHubTransport_Create shall return NULL. ]*/
                LogError("Lower Layer transport not created.");
                free(result);
                result = NULL;
            }
            else
            {
                /*Codes_SRS_IOTHUBTRANSPORT_17_007: [ IoTHubTransport_Create shall create the transport lock by Calling Lock_Init. ]*/
                result->lockHandle = Lock_Init();
                if (result->lockHandle == NULL)
                {
                    /*Codes_SRS_IOTHUBTRANSPORT_17_008: [ If the lock creation fails, IoTHubTransport_Create shall return NULL. ]*/
                    LogError("transport Lock not created.");
                    transportProtocol->IoTHubTransport_Destroy(result->transportLLHandle);
                    free(result);
                    result = NULL;
                }
                else
                {
                    /*Codes_SRS_IOTHUBTRANSPORT_17_001: [ IoTHubTransport_Create shall return a non-NULL handle on success.]*/
                    result->IoTHubTransport_GetHostname = transportProtocol->IoTHubTransport_GetHostname;
                    result->IoTHubTransport_SetOption = transportProtocol->IoTHubTransport_SetOption;
                    result->IoTHubTransport_Create = transportProtocol->IoTHubTransport_Create;
                    result->IoTHubTransport_Destroy = transportProtocol->IoTHubTransport_Destroy;
                    result->IoTHubTransport_Register = transportProtocol->IoTHubTransport_Register;
                    result->IoTHubTransport_Unregister = transportProtocol->IoTHubTransport_Unregister;
                    result->IoTHubTransport_Subscribe = transportProtocol->IoTHubTransport_Subscribe;
                    result->IoTHubTransport_Unsubscribe = transportProtocol->IoTHubTransport_Unsubscribe;
                    result->IoTHubTransport_DoWork = transportProtocol->IoTHubTransport_DoWork;
                    result->IoTHubTransport_SetRetryPolicy = transportProtocol->IoTHubTransport_SetRetryPolicy;
                    result->IoTHubTransport_GetSendStatus = transportProtocol->IoTHubTransport_GetSendStatus;
                }
            }
        }
    }

    return result;
}

static bool find_by_handle(const void* element, const void* value)
{
    /* data stored at element is device handle */
    const IOTHUB_CLIENT_CORE_HANDLE * guess = (const IOTHUB_CLIENT_CORE_HANDLE *)element;
    const IOTHUB_CLIENT_CORE_HANDLE match = (const IOTHUB_CLIENT_CORE_HANDLE)value;
    return (*guess == match);
}

void IoTHubTransport_Destroy(TRANSPORT_HANDLE transportHandle)
{
    /*Codes_SRS_IOTHUBTRANSPORT_17_011: [ IoTHubTransport_Destroy shall do nothing if transportHandle is NULL. ]*/
    if (transportHandle != NULL)
    {
        TRANSPORT_HANDLE_DATA * transportData = (TRANSPORT_HANDLE_DATA*)transportHandle;
        /*Codes_SRS_IOTHUBTRANSPORT_17_010: [ IoTHubTransport_Destroy shall free all resources. ]*/
        Lock_Deinit(transportData->lockHandle);
        (transportData->IoTHubTransport_Destroy)(transportData->transportLLHandle);
        free(transportHandle);
    }
}

LOCK_HANDLE IoTHubTransport_GetLock(TRANSPORT_HANDLE transportHandle)
{
    LOCK_HANDLE lock;
    if (transportHandle == NULL)
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_013: [ If transportHandle is NULL, IoTHubTransport_GetLock shall return NULL. ]*/
        lock = NULL;
    }
    else
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_012: [ IoTHubTransport_GetLock shall return a handle to the transport lock. ]*/
        TRANSPORT_HANDLE_DATA * transportData = (TRANSPORT_HANDLE_DATA*)transportHandle;
        lock = transportData->lockHandle;
    }
    return lock;
}

TRANSPORT_LL_HANDLE IoTHubTransport_GetLLTransport(TRANSPORT_HANDLE transportHandle)
{
    TRANSPORT_LL_HANDLE llTransport;
    if (transportHandle == NULL)
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_015: [ If transportHandle is NULL, IoTHubTransport_GetLLTransport shall return NULL. ]*/
        llTransport = NULL;
    }
    else
    {
        /*Codes_SRS_IOTHUBTRANSPORT_17_014: [ IoTHubTransport_GetLLTransport shall return a handle to the lower layer transport. ]*/
        TRANSPORT_HANDLE_DATA * transportData = (TRANSPORT_HANDLE_DATA*)transportHandle;
        llTransport = transportData->transportLLHandle;
    }
    return llTransport;
}
