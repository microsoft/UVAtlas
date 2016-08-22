//-------------------------------------------------------------------------------------
// UVAtlas - callbackschemer.h
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//  
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

#include "isochart.h"

namespace Isochart
{

// CCallbackSchemer provides methods to simplify the callback implementation.
// Terms: 
// -Main Task: 
//	A task composed of several sub-tasks.Caller want to get the rate of 
//	progress of main task.
// -Sub-Task:  
//	Small tasks make up a main task. It's relatively easy to decide the total
//	work (approximate value)of a sub-task and the time proportion of the 
//	sub-task in main task. The sum of time proportion of each sub-task composed
//	main task must be 1.0
//
// Methods:
// -SetCallback()
//	Set callback function passed by caller.
// -InitCallbackAdapt()
//	Initialize a sub-task by indicating the total work, time proportion and 
//  complete percent of main task.
// -UpdateCallbackAdapt()
//  Update the total work which has been done in sub task; Check if caller want
//  to report.
// -CheckPointAdapt()
//	Only check if caller want to report.
// -FinishWorkAdapt()
//	Finish a sub-task.
//
// Example:
// 	-A main task A has 2 sub-tasks: B, C
//  -B has nearly 200 steps, costs 35% time of A.
//  -C has nearly 100 steps, costs 65% time of A.
//  To give callers the progress of A. The code is like following: 
//	1.1 InitCallbackAdapt(200, 0.35f, 0); // Init task B.
//  1.2 UpdateCallbackAdapt(1)...UpdateCallbackAdapt(1)... // Perform task B 
//  1.3 FinishWorkAdapt() //Finish task B
//	2.1 InitCallbackAdapt(100, 0.65f, 0.35); // Init task C.
//  2.2 UpdateCallbackAdapt(1)...UpdateCallbackAdapt(1)... // Perform task C 
//  2.3 FinishWorkAdapt() //Finish task C

class CCallbackSchemer
{
public:
    CCallbackSchemer():
        m_pCallback(nullptr),
        m_dwTotalStage(0),
        m_dwDoneStage(0)
        {}		

    void SetCallback(
        LPISOCHARTCALLBACK pCallback,  
        float Frequency);

    void SetStage(
        unsigned int TotalStageCount,
        unsigned int DoneStageCount);
    void IncreaseDoneStage(){ m_dwDoneStage++; }


    void InitCallBackAdapt(
        size_t dwTaskWork,
        float fPercentOfAllTasks, 
        float fBase);

    HRESULT UpdateCallbackAdapt(size_t dwDone);
    HRESULT UpdateCallbackDirectly(float fPercent);

    HRESULT CheckPointAdapt();

    HRESULT FinishWorkAdapt();

    float PercentInAllStage();

private:
    
    LPISOCHARTCALLBACK m_pCallback; // Callback function
    float m_fCallbackFrequence;// The frequency to call callback function.

    size_t m_dwTotalWork;	// Steps of current sub-task.
    size_t m_dwWorkDone;		// Steps have been completed in the sub-task.
    size_t m_dwNextCallback;	// The next point to call callback function.
    size_t m_dwCallbackDelta;// The frequence to call callback, indicate hwo 
                            // many steps should be done before next callback
    size_t m_dwWaitPoint;	// When sub-task reach this point, don't update the
                            // rate of progress until FinishWorkAdapt 
    size_t m_dwWaitCount;	// Work with m_dwWaitPoint. keep the frequence of 
                            // calling callback, but not update rate of progress
    bool m_bIsWaitToFinish;	// Inidicate sub-task has reached to wait point.
    
    float m_fPercentScale;	// One step of sub-task can complete how many work 
                            //(in percent)of main work 
    float m_fBase;	// The complete percent of main task before performing 
                    //current sub-task.

    unsigned int m_dwTotalStage;
    unsigned int m_dwDoneStage;

    float m_fPercentOfAllTasks;

};

inline void CCallbackSchemer::SetCallback(
    LPISOCHARTCALLBACK pCallback,  
    float Frequency)
{
    m_pCallback = pCallback;
    m_fCallbackFrequence = Frequency;
}

inline void CCallbackSchemer::SetStage(
    unsigned int TotalStageCount,
    unsigned int DoneStageCount)
{
    m_dwTotalStage = TotalStageCount;
    m_dwDoneStage = DoneStageCount;
}

inline float CCallbackSchemer::PercentInAllStage()
{
    float fPercent = m_fBase+m_dwWorkDone*m_fPercentScale;
    return (m_dwDoneStage * 1.0f)/ m_dwTotalStage + fPercent/ m_dwTotalStage;
}

inline void CCallbackSchemer::InitCallBackAdapt(
    size_t dwTaskWork, 
    float fPercentOfAllTasks, 
    float fBase)
{
    if (!m_pCallback)
    {
        return;
    }

    // dwTaskWork steps in current sub-task
    m_dwTotalWork = dwTaskWork;
    m_dwWorkDone = 0;

    if (0 == dwTaskWork)
    {
        return;
    }
    // Call callback function per m_dwCallbackDelta steps
    m_dwCallbackDelta = static_cast<size_t>(
        m_fCallbackFrequence * dwTaskWork /fPercentOfAllTasks);

    if (m_dwCallbackDelta == 0)
    {
        m_dwCallbackDelta = 1;
    }

    m_dwNextCallback = m_dwCallbackDelta;

    // One step in current sub-task finished how many percent time of main task
    m_fPercentScale = 1.0f / dwTaskWork * fPercentOfAllTasks;

    m_bIsWaitToFinish = false;
    m_dwWaitPoint = dwTaskWork - m_dwCallbackDelta;
    
    m_fBase = fBase;
    m_fPercentOfAllTasks = fPercentOfAllTasks;
}

inline HRESULT CCallbackSchemer::UpdateCallbackDirectly(float fPercent)
{
    if (!m_pCallback)
    {
        return S_OK;
    }

    if (fPercent > 1.0f) fPercent = 1.0f;
    if (fPercent < 0.0f) fPercent = 0.0f;

    float fRealPercent = m_fBase+m_fPercentOfAllTasks*fPercent;
    fRealPercent = (m_dwDoneStage * 1.0f)/ m_dwTotalStage + fRealPercent/ m_dwTotalStage;
    return m_pCallback(fRealPercent);
}

inline HRESULT CCallbackSchemer::UpdateCallbackAdapt(size_t dwDone)
{
    if (!m_pCallback || 0 == dwDone)
    {
        return S_OK;
    }
    
    bool bFire = false;

    // Has reached to wait point, not update current progress, but still check 
    // if caller want to abort.
    if (m_bIsWaitToFinish)
    {	
        // Keep the frequency to call callback function
        m_dwWaitCount += dwDone;  
        if (m_dwWaitCount >= m_dwCallbackDelta)
        {
            while (m_dwWaitCount >= m_dwCallbackDelta)
            {
                m_dwWaitCount -= m_dwCallbackDelta;
            }
            bFire = true;				
        }
    }
    else
    {
        m_dwWorkDone += dwDone;		

        // if reach to the wait point,  set wait flag, initialize m_dwWaitCount 
        // to control the frequency to call callback function
        if (m_dwWorkDone >= m_dwWaitPoint) 
        {
            m_dwWorkDone = m_dwWaitPoint;
            bFire = true;			
            m_bIsWaitToFinish = true;
            m_dwWaitCount = 0;
        }
        // Not reach the wait point yet, update progress.
        else
        {
            while (m_dwWorkDone > m_dwNextCallback)
            {
                bFire = true;
                m_dwNextCallback += m_dwCallbackDelta;
            }
        }
    }
    if (bFire)
    {
        return m_pCallback(PercentInAllStage());
    }

    return S_OK;
}


inline HRESULT CCallbackSchemer::CheckPointAdapt()
{
    if (!m_pCallback)
    {
        return S_OK;
    }

    // Not update progress, only check if caller want to abort.
    return m_pCallback(PercentInAllStage());
}

inline HRESULT CCallbackSchemer::FinishWorkAdapt()
{
    if (!m_pCallback)
    {
        return S_OK;
    }

    m_dwWorkDone = m_dwTotalWork; // Indicate current sub-task has finished.
    return m_pCallback(
        PercentInAllStage());		
}

}
