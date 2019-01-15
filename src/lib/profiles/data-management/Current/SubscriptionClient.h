/*
 *
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file defines subscription client for Weave
 *      Data Management (WDM) profile.
 *
 */

#ifndef _WEAVE_DATA_MANAGEMENT_SUBSCRIPTION_CLIENT_CURRENT_H
#define _WEAVE_DATA_MANAGEMENT_SUBSCRIPTION_CLIENT_CURRENT_H

#include <Weave/Core/WeaveCore.h>

#include <Weave/Profiles/data-management/TraitCatalog.h>
#include <Weave/Profiles/data-management/TraitPathStore.h>
#include <Weave/Profiles/data-management/UpdateClient.h>
#include <Weave/Profiles/data-management/UpdateEncoder.h>
#include <Weave/Profiles/data-management/EventLogging.h>
#include <Weave/Profiles/status-report/StatusReportProfile.h>

namespace nl {
namespace Weave {
namespace Profiles {
namespace WeaveMakeManagedNamespaceIdentifier(DataManagement, kWeaveManagedNamespaceDesignation_Current) {

typedef uint32_t PropertyPathHandle;
typedef uint16_t PropertySchemaHandle;
typedef uint16_t PropertyDictionaryKey;
typedef uint16_t TraitDataHandle;

/**
 * @class IWeaveWDMMutex
 *
 * @brief Interface of a mutex object. Mutexes of this type are implemented by the application
 * and used in WDM to protect data structures that can be accessed from multiple threads.
 * Implementations of this interface must behave like a recursive lock.
 */

class IWeaveWDMMutex
{
public:
    virtual void Lock(void)   = 0;
    virtual void Unlock(void) = 0;
};

class SubscriptionClient
{
public:
    enum
    {
        kNoTimeOut = 0,

        // Note the WDM spec says 0x7FFFFFFF, but Weave implementation can only hold timeout of much shorter
        // 32-bit in milliseconds, which is about 1200 hours
        kMaxTimeoutSec = 3600000,
    };

    enum EventID
    {
        // Marks the end of this subscription. The parameters sent to
        // the EventCallback will indicate whether a resubscribe will
        // be automatically attempted.
        // If no retry will be attempted, the state of the client will
        // be Aborted. No more downcalls into the application will
        // be made.
        // Otherwise, the state will not be aborted or freed. Downcalls
        // will continue.
        // The application is allowed to abort or free in this state.
        // The parameters sent will also include an error code indicating
        // the reason for ending the subscription. There are no guarantees
        // for the state of mBinding or mEC.
        // The subscription could have been terminated for a number of reasons
        // (WRM ACK missing, EC allocation failure, response timeout,...)
        // Some possible error codes generated by the client:
        // WEAVE_ERROR_INVALID_MESSAGE_TYPE if some unrecognized message is received
        // WEAVE_ERROR_TIMEOUT if an ack is not received or if a liveness check fails
        // WEAVE_ERROR_INVALID_STATE if messages are received in an unexpected state
        // WEAVE_ERROR_STATUS_REPORT_RECEIVED if a status report is received
        // WEAVE_ERROR_INVALID_ARGUMENT if subscribe request fields are invalid
        kEvent_OnSubscriptionTerminated = 1,

        // Last chance to adjust EC, mEC is valid and can be tuned for timeout settings
        // Don't touch anything else, and don't close the EC
        kEvent_OnExchangeStart = 2,

        // Sent as the client is creating the subscribe request
        kEvent_OnSubscribeRequestPrepareNeeded = 3,

        // cancel, abort, and free are allowed
        kEvent_OnSubscriptionEstablished = 4,

        // cancel, abort, and free are allowed
        kEvent_OnNotificationRequest = 5,

        // cancel, abort, and free are allowed
        kEvent_OnNotificationProcessed = 6,

        // An event stream has been received
        kEvent_OnEventStreamReceived = 7,

        // An event indicating subscription activity
        kEvent_OnSubscriptionActivity = 8,

#if WEAVE_CONFIG_ENABLE_WDM_UPDATE
        // The update of a trait path has succeeded or failed
        kEvent_OnUpdateComplete       = 9,

        // No more paths need to be updated
        kEvent_OnNoMorePendingUpdates = 10,
#endif // WEAVE_CONFIG_ENABLE_WDM_UPDATE
    };

    struct LastObservedEvent
    {
        uint64_t mSourceId;
        uint8_t mImportance;
        uint64_t mEventId;
    };

    // union of structures for each event some of them might be empty
    union InEventParam
    {
        void Clear(void) { memset(this, 0, sizeof(*this)); }

        struct
        {
            WEAVE_ERROR mReason;

            bool mIsStatusCodeValid;
            uint32_t mStatusProfileId;
            uint16_t mStatusCode;
            ReferencedTLVData * mAdditionalInfoPtr;
            bool mWillRetry;

            SubscriptionClient * mClient;
        } mSubscriptionTerminated;

        struct
        {
            // Do not close the EC
            nl::Weave::ExchangeContext * mEC;
            SubscriptionClient * mClient;
        } mExchangeStart;

        struct
        {
            SubscriptionClient * mClient;
        } mSubscribeRequestPrepareNeeded;

        struct
        {
            uint64_t mSubscriptionId;
            SubscriptionClient * mClient;
        } mSubscriptionEstablished;

        struct
        {
            // Do not close the EC
            nl::Weave::ExchangeContext * mEC;

            // Do not modify the message content
            PacketBuffer * mMessage;
            SubscriptionClient * mClient;
        } mNotificationRequest;

        struct
        {
            SubscriptionClient * mClient;
        } mNotificationProcessed;

        struct
        {
            nl::Weave::TLV::TLVReader * mReader;
            SubscriptionClient * mClient;
        } mEventStreamReceived;

        struct
        {
            SubscriptionClient * mClient;
        } mSubscriptionActivity;

#if WEAVE_CONFIG_ENABLE_WDM_UPDATE
        struct
        {
            WEAVE_ERROR mReason;
            uint32_t mStatusProfileId;
            uint16_t mStatusCode;
            TraitDataHandle    mTraitDataHandle;
            PropertyPathHandle mPropertyPathHandle;
            SubscriptionClient * mClient;
            bool mWillRetry;
        } mUpdateComplete;
#endif // WEAVE_CONFIG_ENABLE_WDM_UPDATE
    };

    union OutEventParam
    {
        void Clear(void) { memset(this, 0, sizeof(*this)); }

        struct
        {
            TraitPath * mPathList;                   //< Pointer to a list of trait paths
            VersionedTraitPath * mVersionedPathList; //< Pointer to a list of versioned trait paths. If both this and mPathList are
                                                     //non-NULL, the versioned path list is selected
            size_t mPathListSize;                    //< Number of trait paths in mPathList
            LastObservedEvent * mLastObservedEventList; //< A list of the last known events received by the subscriber
            size_t mLastObservedEventListSize;          //< Number of observed events in mLastObservedEventList
            uint32_t mTimeoutSecMin;                    //< Field specifying lower bound of liveness timeout
            uint32_t mTimeoutSecMax;                    //< Field specifying upper bound of liveness timeout
            uint64_t mSubscriptionId;                   //< The subscription ID to use for a mutual subscription
            bool mNeedAllEvents;                        //< Indicates whether the subscriber is interested in events
        } mSubscribeRequestPrepareNeeded;
    };

    struct ResubscribeParam
    {
        typedef enum {
            kSubscription,
            kUpdate
        } RequestType;
        WEAVE_ERROR mReason;  //< Error received on most recent failure
        uint32_t mNumRetries; //< Number of retries, reset on a successful attempt
        RequestType     mRequestType; //< Request being backed off
    };

    /**
     * @brief Callback to pass subscription events to application.
     *
     * @param[in] aAppState     App state pointer set during initialization of
     *                          the SubscriptionClient.
     *
     * @param[in] aEvent        Indicates which event is happening
     *
     * @param[in] aInParam      Struct with additional details about the event
     *
     * @param[out] aOutParam    Information passed back by the application
     */
    typedef void (*EventCallback)(void * const aAppState, EventID aEvent, const InEventParam & aInParam, OutEventParam & aOutParam);

    static void DefaultEventHandler(EventID aEvent, const InEventParam & aInParam, OutEventParam & aOutParam);

    /**
     * @brief Callback to fetch the interval of time to wait before the next
     * resubscribe. Applications are allowed to abort/free in this function
     * if they've decided to give up on resubscribing.
     *
     * @param[in] aAppState         App state pointer set during initialization of
     *                              the SubscriptionClient.
     *
     * @param[in] aInParam          Struct with additional details about the retry
     *
     * @param[out] aOutIntervalMsec Time in milliseconds to wait before next retry
     */
    typedef void (*ResubscribePolicyCallback)(void * const aAppState, ResubscribeParam & aInParam, uint32_t & aOutIntervalMsec);

    static void DefaultResubscribePolicyCallback(void * const aAppState, ResubscribeParam & aInParam, uint32_t & aOutIntervalMsec);

    void InitiateSubscription(void);
    void InitiateCounterSubscription(const uint32_t aLivenessTimeoutSec);

    Binding * GetBinding(void) const;

    uint64_t GetPeerNodeId(void) const;

    WEAVE_ERROR EndSubscription(void);
    void AbortSubscription(void);

    void Free(void);

    WEAVE_ERROR GetSubscriptionId(uint64_t * const apSubscriptionId);

    uint32_t GetLivenessTimeoutMsec(void) const { return mLivenessTimeoutMsec; };
    void SetLivenessTimeoutMsec(uint32_t val) { mLivenessTimeoutMsec = val; }

    void EnableResubscribe(ResubscribePolicyCallback aCallback);
    void DisableResubscribe(void);
    void ResetResubscribe(void);

    bool IsInProgressOrEstablished() { return (mCurrentState >= kState_InProgressOrEstablished_Begin && mCurrentState <= kState_InProgressOrEstablished_End); }

    bool IsRetryEnabled() { return (mResubscribePolicyCallback != NULL); }

    bool IsEstablished() { return (mCurrentState >= kState_Established_Begin && mCurrentState <= kState_Established_End); }
    bool IsEstablishedIdle() { return (mCurrentState == kState_SubscriptionEstablished_Idle); }
    bool IsFree() { return (mCurrentState == kState_Free); }
    bool IsAborting() { return (mCurrentState == kState_Aborting); }
    bool IsInResubscribeHoldoff() { return (mCurrentState == kState_Resubscribe_Holdoff); }

    void IndicateActivity(void);

#if WEAVE_CONFIG_ENABLE_WDM_UPDATE
    void LockUpdateMutex(void);
    void UnlockUpdateMutex(void);

    WEAVE_ERROR FlushUpdate();
    WEAVE_ERROR FlushUpdate(bool aForce);
    WEAVE_ERROR SetUpdated(TraitUpdatableDataSink * aDataSink, PropertyPathHandle aPropertyHandle, bool aIsConditional);
    void DiscardUpdates();
    void SuspendUpdateRetries();

    bool IsUpdatePendingOrInProgress() { return (kPendingSetEmpty != mPendingSetState || IsUpdateInProgress()); }
#endif // WEAVE_CONFIG_ENABLE_WDM_UPDATE


private:
    friend class SubscriptionEngine;
    friend class TestTdm;
    friend class TestWdm;
    friend class WdmUpdateEncoderTest;
    friend class MockWdmSubscriptionInitiatorImpl;
    friend class TraitDataSink;
    friend class TraitSchemaEngine;
    friend class UpdateDirtyPathFilter;
    friend class UpdateDictionaryDirtyPathCut;
    friend class UpdateEncoder;

    enum ClientState
    {
        kState_Free        = 0,
        kState_Initialized = 1,

        kState_Subscribing                        = 2,
        kState_Subscribing_IdAssigned             = 3,
        kState_SubscriptionEstablished_Idle       = 4,
        kState_SubscriptionEstablished_Confirming = 5,
        kState_Canceling                          = 6,

        kState_Resubscribe_Holdoff = 7,

        kState_NotifyDataSinkOnAbort_Begin = kState_Subscribing,
        kState_NotifyDataSinkOnAbort_End   = kState_Canceling,
        kState_TimerTick_Begin             = kState_Subscribing,
        kState_TimerTick_End               = kState_Resubscribe_Holdoff,

        // Note that these are the same as the allowed states in NotificationRequestHandler
        kState_InProgressOrEstablished_Begin       = kState_Subscribing,
        kState_InProgressOrEstablished_End         = kState_SubscriptionEstablished_Confirming,

        kState_Established_Begin           = kState_SubscriptionEstablished_Idle,
        kState_Established_End             = kState_SubscriptionEstablished_Confirming,

        kState_Aborting = 8,
    };

    ClientState mCurrentState;

    /**
     * The runtime configuration; i.e. the desired state of the Client.
     */
    enum ClientConfig {
        kConfig_Down,               /**< The default configuration: no subscription, no binding.
                                         The client comes back to this configuration automatically if the responder
                                         sends a Cancel request. */
        kConfig_Initiator,          /**< Subscribe as an initiator */
        kConfig_CounterSubscriber   /**< Start a "counter subscription" */
    };

    bool IsInitiator() { return mConfig == kConfig_Initiator; }
    bool IsCounterSubscriber() { return mConfig == kConfig_CounterSubscriber; }
    bool ShouldSubscribe() { return mConfig > kConfig_Down; }

    ClientConfig mConfig;
    bool mPrevIsPartialChange;
#if WDM_ENABLE_PROTOCOL_CHECKS
    TraitDataHandle mPrevTraitDataHandle;
#endif

    int8_t mRefCount;

    Binding * mBinding;
    nl::Weave::ExchangeContext * mEC;

    void * mAppState;
    EventCallback mEventCallback;

    Binding::EventCallback mAppBindingCallback;
    void * mAppBindingState;

    ResubscribePolicyCallback mResubscribePolicyCallback;

    TraitCatalogBase<TraitDataSink> * mDataSinkCatalog;

    ExchangeContext::Timeout mInactivityTimeoutDuringSubscribingMsec;
    uint32_t mLivenessTimeoutMsec;
    uint64_t mSubscriptionId;

    // retry params
    uint32_t mRetryCounter;

    // Do nothing
    SubscriptionClient(void);

    void InitAsFree(void);
    void Reset(void);

    // AddRef to Binding
    // store pointers to binding and delegate
    // null out EC
    WEAVE_ERROR Init(Binding * const apBinding, void * const apAppState, EventCallback const aEventCallback,
                     const TraitCatalogBase<TraitDataSink> * const apCatalog,
                     const uint32_t aInactivityTimeoutDuringSubscribingMsec,
                     IWeaveWDMMutex * aUpdateMutex);

    void _InitiateSubscription(void);
    WEAVE_ERROR SendSubscribeRequest(void);

    void _AbortSubscription();
    void _Cleanup();

    WEAVE_ERROR ProcessDataList(nl::Weave::TLV::TLVReader & aReader);

    void _AddRef(void);
    void _Release(void);

    void HandleSubscriptionTerminated(bool aWillRetry, WEAVE_ERROR aReason,
                                      nl::Weave::Profiles::StatusReporting::StatusReport * aStatusReportPtr);
    WEAVE_ERROR _PrepareBinding(void);

    WEAVE_ERROR ReplaceExchangeContext(void);

    void FlushExistingExchangeContext(const bool aAbortNow = false);

    void NotificationRequestHandler(nl::Weave::ExchangeContext * aEC, const nl::Inet::IPPacketInfo * aPktInfo,
                                    const nl::Weave::WeaveMessageInfo * aMsgInfo, PacketBuffer * aPayload);

#if WDM_ENABLE_SUBSCRIPTION_CANCEL
    void CancelRequestHandler(nl::Weave::ExchangeContext * aEC, const nl::Inet::IPPacketInfo * aPktInfo,
                              const nl::Weave::WeaveMessageInfo * aMsgInfo, PacketBuffer * aPayload);
#endif // WDM_ENABLE_SUBSCRIPTION_CANCEL

    void TimerEventHandler(void);
    WEAVE_ERROR RefreshTimer(void);

    void SetRetryTimer(WEAVE_ERROR aReason);

    void MoveToState(const ClientState aTargetState);

    const char * GetStateStr(void) const;

    static void BindingEventCallback(void * const apAppState, const Binding::EventType aEvent,
                                     const Binding::InEventParam & aInParam, Binding::OutEventParam & aOutParam);
    static void OnTimerCallback(System::Layer * aSystemLayer, void * aAppState, System::Error aErrorCode);
    static void DataSinkOperation_NoMoreData(void * const apOpState, TraitDataSink * const apDataSink);
    static void OnSendError(ExchangeContext * aEC, WEAVE_ERROR aErrorCode, void * aMsgSpecificContext);
    static void OnResponseTimeout(nl::Weave::ExchangeContext * aEC);
    static void OnMessageReceivedFromLocallyInitiatedExchange(nl::Weave::ExchangeContext * aEC,
                                                              const nl::Inet::IPPacketInfo * aPktInfo,
                                                              const nl::Weave::WeaveMessageInfo * aMsgInfo, uint32_t aProfileId,
                                                              uint8_t aMsgType, PacketBuffer * aPayload);

#if WEAVE_CONFIG_ENABLE_WDM_UPDATE

    IWeaveWDMMutex * mUpdateMutex;

    struct UpdateRequestContext
    {
        void Reset();

        size_t mItemInProgress;
        PropertyPathHandle mNextDictionaryElementPathHandle;
        uint32_t mUpdateRequestIndex;
        bool mIsPartialUpdate;
    };
    uint32_t mUpdateRetryCounter;
    bool mSuspendUpdateRetries;
    bool mUpdateRetryScheduled;
    bool mUpdateFlushScheduled;

    void StartUpdateRetryTimer(WEAVE_ERROR aReason);
    static void OnUpdateTimerCallback(System::Layer * aSystemLayer, void * aAppState, System::Error);
    static void OnUpdateScheduleWorkCallback(System::Layer * aSystemLayer, void * aAppState, System::Error);
    void UpdateTimerEventHandler(void);

    uint32_t GetMaxUpdateSize(void) const { return mMaxUpdateSize == 0 ? UINT16_MAX : mMaxUpdateSize; }
    void SetMaxUpdateSize(const uint32_t aMaxPayload);

    // Methods to encode and send update requests
    void FormAndSendUpdate();
    WEAVE_ERROR SendSingleUpdateRequest(void);
    static WEAVE_ERROR AddElementFunc(UpdateEncoder * aEncoder, void *apCallState, TLV::TLVWriter & aOuterWriter);
    void SetUpdateStartVersions(void);

    // Methods to handle update response and exchange failures (OnResponseTimeout, OnSendError)
    void OnUpdateResponse(WEAVE_ERROR aReason, nl::Weave::Profiles::StatusReporting::StatusReport * apStatus);
    void OnUpdateNoResponse(WEAVE_ERROR aReason);
    static bool WillRetryUpdate(WEAVE_ERROR aErr, uint32_t aStatusProfileId, uint16_t aStatusCode);

    // Methods to purge obsolete pending paths
    WEAVE_ERROR PurgePendingUpdate(void);
    void MarkFailedPendingPaths(TraitDataHandle aTraitDataHandle, TraitUpdatableDataSink &aSink, const DataVersion &aLatestVersion);
    WEAVE_ERROR PurgeFailedPendingPaths(WEAVE_ERROR aErr, size_t &aCount);

    // Methods to handle potential data loss due to notifications received with updates pending or in progress
    bool FilterNotifiedPath(TraitDataHandle aTraitDataHandle, PropertyPathHandle aPropertyPathHandle, const TraitSchemaEngine * const aSchemaEngine);
    void ClearPotentialDataLoss(TraitDataHandle aTraitDataHandle, TraitUpdatableDataSink &aUpdatableSink);
    bool CheckForSinksWithDataLoss();
    static void CheckForSinksWithDataLossIteratorCb(void * aDataSink, TraitDataHandle aDataHandle, void * aContext);

    // Methods to fail everything and notify the application
    void PurgeAndNotifyFailedPaths(WEAVE_ERROR aErr, TraitPathStore &aPathStore, size_t &aCount);

    // Methods to manage the pending set and in-progress list
    enum PendingSetState {
        kPendingSetEmpty,
        kPendingSetOpen,
        kPendingSetReady
    };
    void SetPendingSetState(PendingSetState aState);
    WEAVE_ERROR MovePendingToInProgress(void);
    WEAVE_ERROR AddItemPendingUpdateSet(const TraitPath &aItem, const TraitSchemaEngine * const aSchemaEngine);
    WEAVE_ERROR MoveInProgressToPending(void);

    // Tracking if a payload is in flight
    bool IsUpdateInFlight() { return mUpdateInFlight; }
    void SetUpdateInFlight() { mUpdateInFlight = true; }
    void ClearUpdateInFlight() { mUpdateInFlight = false; }

    // Knowing if an update is pending or in progress
    bool IsUpdateInProgress() { return (false == mInProgressUpdateList.IsEmpty()); }
    bool IsReadyToSendNewUpdate() { return (mPendingSetState == kPendingSetReady && false == IsUpdateInProgress()); }

    // Methods to notify the application
    void UpdateCompleteEventCbHelper(const TraitPath &aTraitPath, uint32_t aStatusProfileId, uint16_t aStatusCode, WEAVE_ERROR aReason, bool aWillRetry);
    void NoMorePendingEventCbHelper(void);

    // Other methods related to mUpdateClient
    static void UpdateEventCallback(void * const aAppState, UpdateClient::EventType aEvent, const UpdateClient::InEventParam & aInParam, UpdateClient::OutEventParam & aOutParam);
    void AbortUpdates(WEAVE_ERROR);


    // Index of the TraitUpdatableDataSink instances stored in mDataSinkCatalog
    /**
     * Updatable trait instance context
     */
    struct UpdatableTIContext
    {
        void Init(TraitUpdatableDataSink *aUpdatableDataSink, TraitDataHandle aTraitDataHandle)
        {
            mUpdatableDataSink = aUpdatableDataSink;
            mTraitDataHandle = aTraitDataHandle;
        }

        TraitDataHandle mTraitDataHandle;
        TraitUpdatableDataSink *mUpdatableDataSink;
    };
    static void InitUpdatableSinkTrait(void *aDataSink, TraitDataHandle aDataHandle, void *aContext);
    static void CleanupUpdatableSinkTrait(void *aDataSink, TraitDataHandle aDataHandle, void *aContext);
    UpdatableTIContext *GetUpdatableTIContextList(void) { return mClientTraitInfoPool; }
    uint32_t GetNumUpdatableTraitInstances(void) { return mNumUpdatableTraitInstances; }

    // Data members for WDM Update

    UpdatableTIContext mClientTraitInfoPool[WDM_CLIENT_MAX_NUM_UPDATABLE_TRAITS];
    uint16_t mNumUpdatableTraitInstances;

    UpdateRequestContext mUpdateRequestContext;
    uint16_t mMaxUpdateSize;
    bool mUpdateInFlight;

    // Flags used with mInProgressUpdateList
    enum {
        kFlag_ForceMerge = 0x4, /**< In UpdateRequest, DataElements are encoded with the "replace" format by
                                  default; this flag is used to force the encoding of
                                  dictionaries so that the items are merged.
                                  */
        kFlag_Private    = 0x8, /**< The path was created internally by the engine
                                  to encode a dictionary in its own separate
                                  DataElement; the application is notified about it
                                  only if the update fails.
                                  */

    };

    PendingSetState mPendingSetState;
    TraitPathStore mPendingUpdateSet;
    TraitPathStore::Record mPendingStore[WDM_UPDATE_MAX_ITEMS_IN_TRAIT_DIRTY_PATH_STORE];

    TraitPathStore mInProgressUpdateList;
    TraitPathStore::Record mInProgressStore[WDM_UPDATE_MAX_ITEMS_IN_TRAIT_DIRTY_PATH_STORE];

    UpdateClient mUpdateClient;
    UpdateEncoder mUpdateEncoder;
#endif // WEAVE_CONFIG_ENABLE_WDM_UPDATE
};

}; // namespace WeaveMakeManagedNamespaceIdentifier(DataManagement, kWeaveManagedNamespaceDesignation_Current)
}; // namespace Profiles
}; // namespace Weave
}; // namespace nl

#endif // _WEAVE_DATA_MANAGEMENT_SUBSCRIPTION_CLIENT_CURRENT_H
