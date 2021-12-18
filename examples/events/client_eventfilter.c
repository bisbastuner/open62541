/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

/**
 *
 * Attention! WIP: The event-filter mechanism is currently "Work in progress" and will be completed
 * soon. Some of the below defined filters are currently not working and will be completed
 * in on of the next event-filter-batches.
 *
 * When using events, the client is mostly only interested in a small selection of events.
 * To avoid unnecessary event transmission, the client can configure filters. These filters
 * are part of the event-subscription configuration.
 *
 * The following example shows the general use and configuration of event-filters. This
 * example is intended to be used together with the "server_random_events" example. The
 * server example can emit events, which mach the below defined event-filters.
 *
 * */

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/util.h>

#include <signal.h>

#define USE_FILTER_OR_TYPEOF

static UA_Boolean running = true;
const size_t nSelectClauses = 2;
const size_t nWhereClauses = 1;

/**
 * Setting up SelectClauses
 * ^^^^^^^^^^^^^^^^^^^
 * The SelectClauses specifies the EventFields of an Event.
 * It is represented in a SimpleAttributeOperand Array.
 * Each SimpleAttributeOperand represents one EventField that should be returned.
 * In this Example we are selecting two SimpleAttributeOperands to be returned, one as a Message and one as Severity.
 */
static UA_SimpleAttributeOperand *
setupSelectClauses(void) {
    UA_SimpleAttributeOperand *selectClauses = (UA_SimpleAttributeOperand*)
        UA_Array_new(nSelectClauses, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]);
    if(!selectClauses)
        return NULL;
    for(size_t i =0; i<nSelectClauses; ++i) {
        UA_SimpleAttributeOperand_init(&selectClauses[i]);
    }
    //configure Message field
    selectClauses[0].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
    selectClauses[0].browsePathSize = 1;
    selectClauses[0].browsePath = (UA_QualifiedName*)
        UA_Array_new(selectClauses[0].browsePathSize, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]);
    if(!selectClauses[0].browsePath) {
        UA_SimpleAttributeOperand_delete(selectClauses);
        return NULL;
    }
    selectClauses[0].attributeId = UA_ATTRIBUTEID_VALUE;
    selectClauses[0].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "Message");
    //configure Severity field
    selectClauses[1].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
    selectClauses[1].browsePathSize = 1;
    selectClauses[1].browsePath = (UA_QualifiedName*)
        UA_Array_new(selectClauses[1].browsePathSize, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]);
    if(!selectClauses[1].browsePath) {
        UA_SimpleAttributeOperand_delete(selectClauses);
        return NULL;
    }
    selectClauses[1].attributeId = UA_ATTRIBUTEID_VALUE;
    selectClauses[1].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "Severity");

    return selectClauses;
}

static UA_StatusCode
setupOperandArrays(UA_ContentFilter *contentFilter){
    for(size_t i =0; i< contentFilter->elementsSize; ++i) {  /* Set Operands Arrays */
        contentFilter->elements[i].filterOperands = (UA_ExtensionObject*)
            UA_Array_new(
                contentFilter->elements[i].filterOperandsSize, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]);
        if (!contentFilter->elements[i].filterOperands){
            UA_ContentFilter_clear(contentFilter);
            return UA_STATUSCODE_BADOUTOFMEMORY;
        }
        for(size_t n =0; n< contentFilter->elements[i].filterOperandsSize; ++n) {
            UA_ExtensionObject_init(&contentFilter->elements[i].filterOperands[n]);
        }
    }
    return UA_STATUSCODE_GOOD;
}

static void
setupOrFilter(UA_ContentFilterElement *element){
    element->filterOperands[0].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    element->filterOperands[0].encoding = UA_EXTENSIONOBJECT_DECODED;
    element->filterOperands[1].content.decoded.type = &UA_TYPES[UA_TYPES_ELEMENTOPERAND];
    element->filterOperands[1].encoding = UA_EXTENSIONOBJECT_DECODED;
    UA_ElementOperand *firstElementOperand = UA_ElementOperand_new();
    UA_ElementOperand_init(firstElementOperand);
    firstElementOperand->index = 1;
    UA_ElementOperand *secondElementOperand = UA_ElementOperand_new();
    UA_ElementOperand_init(secondElementOperand);
    secondElementOperand->index = 2;
    element->filterOperands[0].content.decoded.data = firstElementOperand;
    element->filterOperands[1].content.decoded.data = secondElementOperand;
}

static void
setupOfTypeFilter(UA_ContentFilterElement *element, UA_UInt32 typeId){
    element->filterOperands[0].content.decoded.type = &UA_TYPES[UA_TYPES_LITERALOPERAND];
    element->filterOperands[0].encoding = UA_EXTENSIONOBJECT_DECODED;
    UA_LiteralOperand *literalOperand = UA_LiteralOperand_new();
    UA_LiteralOperand_init(literalOperand);
    UA_NodeId *nodeId = UA_NodeId_new();
    UA_NodeId_init(nodeId);
    nodeId->namespaceIndex = 0;
    nodeId->identifierType = UA_NODEIDTYPE_NUMERIC;
    nodeId->identifier.numeric = typeId;
    UA_Variant_setScalar(&literalOperand->value, nodeId, &UA_TYPES[UA_TYPES_NODEID]);
    element->filterOperands[0].content.decoded.data = literalOperand;
}

/**
 * Setting up WhereClauses
 * ^^^^^^^^^^^^^^^^^^^
 * The WhereClause defines the filtering criteria of an EventFilter, represented as an
 * ContentFilter structure. The ContentFilter structure contains "ContentFilterElements",
 * which contain a operator and a array of operands. The ContentFilter has a Tree-structure.
 * The example allows multiple content-filters (where clauses) which can be selected
 * with the filterSelection paramete.
 *
 * filterSelection 0:
 *      ( (OfType AUDITEVENTTYPE ) (or) (OfType EVENTQUEUEOVERFLOWEVENTTYPE) )
 */
static UA_StatusCode
setupWhereClauses(UA_ContentFilter *contentFilter, UA_UInt16 whereClauseSize, UA_UInt16 filterSelection){
    UA_ContentFilter_init(contentFilter);
    contentFilter->elementsSize = whereClauseSize;
    contentFilter->elements  = (UA_ContentFilterElement *)
        UA_Array_new(contentFilter->elementsSize, &UA_TYPES[UA_TYPES_CONTENTFILTERELEMENT]);
    if(!contentFilter->elements)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    for(size_t i =0; i < contentFilter->elementsSize; ++i) {
        UA_ContentFilterElement_init(&contentFilter->elements[i]);
    }
    UA_StatusCode result = UA_STATUSCODE_GOOD;
    switch(filterSelection) {
        case 0:
            contentFilter->elements[0].filterOperator = UA_FILTEROPERATOR_OR;
            contentFilter->elements[1].filterOperator = UA_FILTEROPERATOR_OFTYPE;
            contentFilter->elements[2].filterOperator = UA_FILTEROPERATOR_OFTYPE;
            contentFilter->elements[0].filterOperandsSize = 2;
            contentFilter->elements[1].filterOperandsSize = 1;
            contentFilter->elements[2].filterOperandsSize = 1;
            /* Setup Operand Arrays */
            result = setupOperandArrays(contentFilter);
            if(result != UA_STATUSCODE_GOOD){
                UA_ContentFilter_clear(contentFilter);
                return UA_STATUSCODE_BADCONFIGURATIONERROR;
            }
            /* first Element (OR) */
            setupOrFilter(&contentFilter->elements[0]);
            /* second Element (OfType) */
            setupOfTypeFilter(&contentFilter->elements[1], 60443);
            /* third Element (OfType) */
            setupOfTypeFilter(&contentFilter->elements[2], UA_NS0ID_EVENTQUEUEOVERFLOWEVENTTYPE);
            break;
        default:
            UA_ContentFilter_clear(contentFilter);
            return UA_STATUSCODE_BADCONFIGURATIONERROR;
    }
    return UA_STATUSCODE_GOOD;
}

static void
handler_events(UA_Client *client, UA_UInt32 subId, void *subContext,
               UA_UInt32 monId, void *monContext,
               size_t nEventFields, UA_Variant *eventFields) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Notification");

    for(size_t i = 0; i < nEventFields; ++i) {
        if(UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_UINT16])) {
            UA_UInt16 severity = *(UA_UInt16 *)eventFields[i].data;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Severity: %u", severity);
        } else if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_LOCALIZEDTEXT])) {
            UA_LocalizedText *lt = (UA_LocalizedText *)eventFields[i].data;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Message: '%.*s'", (int)lt->text.length, lt->text.data);
        }
        else {
#ifdef UA_ENABLE_TYPEDESCRIPTION
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Don't know how to handle type: '%s'", eventFields[i].type->typeName);
#else
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Don't know how to handle type, enable UA_ENABLE_TYPEDESCRIPTION "
                        "for typename");
#endif
        }
    }
}

static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    if(argc < 2) {
        printf("Usage: tutorial_client_events <opc.tcp://server-url>\n");
        return EXIT_FAILURE;
    }

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    UA_StatusCode retval = UA_Client_connect(client, argv[1]);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

    /* Create a subscription */
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request,
                                                                            NULL, NULL, NULL);
    if(response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }
    UA_UInt32 subId = response.subscriptionId;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Create subscription succeeded, id %u", subId);

    /* Add a MonitoredItem */
    UA_MonitoredItemCreateRequest item;
    UA_MonitoredItemCreateRequest_init(&item);
    item.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER); // Root->Objects->Server
    item.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    item.monitoringMode = UA_MONITORINGMODE_REPORTING;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClauses = setupSelectClauses();
    filter.selectClausesSize = nSelectClauses;
    retval = setupWhereClauses(&filter.whereClause, 3, 0);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

    item.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED;
    item.requestedParameters.filter.content.decoded.data = &filter;
    item.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];

    UA_UInt32 monId = 0;
    UA_MonitoredItemCreateResult result =
        UA_Client_MonitoredItems_createEvent(client, subId,
                                             UA_TIMESTAMPSTORETURN_BOTH, item,
                                             &monId, handler_events, NULL);

    if(result.statusCode != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Could not add the MonitoredItem with %s", UA_StatusCode_name(retval));
        goto cleanup;
    } else {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Monitoring 'Root->Objects->Server', id %u", response.subscriptionId);
    }

    monId = result.monitoredItemId;

    while(running)
        retval = UA_Client_run_iterate(client, 100);

    /* Delete the subscription */
    cleanup:
    UA_MonitoredItemCreateResult_clear(&result);
    UA_Client_Subscriptions_deleteSingle(client, response.subscriptionId);
    UA_Array_delete(filter.selectClauses, nSelectClauses, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]);
    UA_Array_delete(filter.whereClause.elements, filter.whereClause.elementsSize, &UA_TYPES[UA_TYPES_CONTENTFILTERELEMENT]);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
