#include "111.h"
#include "Alarm.h"
#include "DevAlarm.h"
#include "APIs/Alarm.h"
#include "Manager/ILog.h"
#include "Comm/VF_Frontboard.h"
#include "Comm/ConfigAlarm.h"
#include "CommDefs.h"
#include "uuid/uuid.h"
#include "Media/ICamera.h"
#include "22222.h"
#define NLEVER 6
#define CALARM_ALL_CHANNELS -1
#define ALARMIN_LATCH_DELAY 0    //报警输入延时时间的默认值

extern SYSTEM_TIME         g_StartTime;            //开机时间
extern int g_nNetAlarmIn;

IAlarmManager* IAlarmManager::instance()
{
	static IAlarmManager* _ins = NULL;

	if(NULL == _ins)
	{
		_ins =dynamic_cast<IAlarmManager*>(CAlarm::instance());
	}

	return _ins;

}
// 得到报警输入路数
int IAlarmManager::getInChannels()
{
	return CDevAlarm::GetInSlots();
}
// 得到报警输出路数
int IAlarmManager::getOutChannels()
{
	return CDevAlarm::GetOutSlots();
}


/*-----------------------------------------------------------------------
    CAlarm类的静态成员变量，定义为静态是因为CAlarmManager对象在一开始的
    初始化函数中就要被调用；
-----------------------------------------------------------------------*/
PATTERN_SINGLETON_IMPLEMENT(CAlarm);
void CAlarm::ThreadProc(uint arg)
{
    while (m_thread.getLoop())                //读取并分发四种报警信号
    {
    	
    	uint64 net_alarm_state = 0;
		uint64 local_alarm_state = 0;

		local_alarm_state = m_pDevAlarm->GetInState();
		Alarm(local_alarm_state, appEventAlarmLocal);

        if (m_nNetAlarmIn > 0)
        {
        	uint64 netState = 0;		
			netState = GetDigitAlarmInState();
            //net_alarm_state = ( netState<<  m_nAlarmIn);
			Alarm(netState, appEventAlarmNet);
        }
        SystemSleep(400);
    }
}

CAlarm::CAlarm() : 
    m_cAlarmTimer("Alarm-timer"),
    m_sigBuffer(32)
{
	m_pDigitAlarm = NULL;
	m_nAlarmOut = 0;
	m_nAlarmIn = 0;
	m_nNetAlarmIn = 0;

    m_dwAlarmState = 0;
    m_dwNetAlarmState = 0;   
    m_dwAlarmOutState = 0;
    m_dwManualAlarmState = 0;

    m_dwAlarmOutType = 0;    //报警输出模式初始化为自动输出.

    const char *pStrName = NULL;
    for (int ii = 0; ii < N_ALM_OUT; ii++)
    {
        for (int i = (int)appEventAlarmLocal; i < (int)appEventAll; i++)
        {
			pStrName = getEventName(i);
			if(pStrName)
			{
            	m_cAlarmOutLinkage[ii].setLinkItem(pStrName);
			}
        }
		m_cAlarmOutLinkage[ii].setLinkItem("appEventFaceFeature");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventFaceDetectAlarm");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventFaceRecognition");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventAudioInvalid");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventAudioVolume");
		
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelLine");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelCross");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelAbandon");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelLeave");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelScene");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelCard");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelColordvt");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelContrast");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelGuard");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelUnfocus");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelTrace");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventExposureBright");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventExposureDark");
		m_cAlarmOutLinkage[ii].setLinkItem("appEventIntelPedestrian");
    }
 
    m_pDevAlarm = CDevAlarm::instance();
}

CAlarm::~CAlarm()
{
}

bool CAlarm::AttachNewAlarm(ALARM_TYPE type, IAlarm* pAalarm)
{
	if(type != ALARM_TYPE_Digital || m_pDigitAlarm != NULL )
	{
		return false;
	}

	m_pDigitAlarm = pAalarm;
	m_nNetAlarmIn = m_pDigitAlarm->GetAlarmInChannels();
	return true;
}
bool CAlarm::DetachNewAlarm(ALARM_TYPE type, IAlarm* pAalarm)
{
	if(type != ALARM_TYPE_Digital || m_pDigitAlarm != pAalarm )
	{
		return false;
	}

    m_nNetAlarmIn = 0;
	return true;
}
bool CAlarm::Start()
{
    comm_infof("CAlarm::Start()>>>>>>>>>\n");
    int i = 0;

	m_nAlarmOut = IAlarmManager::getOutChannels();
	m_nAlarmIn = IAlarmManager::getInChannels();
	m_nNetAlarmIn = 0;
	
    //读取配置信息
    m_CCfgAlarm.update();
    m_CCfgNetAlarm.update();

    //向事件中心注册处理类
    VF_IAppEventManager::instance()->attach(VF_IAppEventManager::Proc(&CAlarm::onAppEvent, this));

    //配置变化回掉函数
    m_CCfgAlarm.attach(this, (TCONFIG_PROC)&CAlarm::onConfigAlarm);
    m_CCfgNetAlarm.attach(this, (TCONFIG_PROC)&CAlarm::onConfigNetAlarm);
	
    for (i = 0; i < N_PTZ_ALARM; i++)
    {
        m_dwAlarmPtzState[i] = 0;
    }
    

    for (int k = 0; k < N_ALM_IN; k++)
    {
        m_iAlarmLatchDelay[k] = -1;
    }    
    for (int k = 0; k < N_MAX_CH; k++)
    {
        m_iBeepstate[k] = ALARMIN_LATCH_DELAY;
    }

/*-----------------------------------------------------------------------
    启动报警模块中的线程和定时器
-----------------------------------------------------------------------*/
	m_cAlarmTimer.Start(this, (VD_TIMERPROC)&CAlarm::OnAlarmDelayTimer, 0, 1000);
   
    /*-----------------------------------------------------------------------
        启动报警模块中的线程和定时器
    -----------------------------------------------------------------------*/
	m_thread.run("Alarm", this, (ASYNPROC)&CAlarm::ThreadProc, 0, 0);
    return TRUE;
}

bool CAlarm::Stop()
{
    comm_infof("CAlarm::Stop()>>>>>>>>>\n");
    
/*-----------------------------------------------------------------------
    销毁线程和停止定时器
-----------------------------------------------------------------------*/
    m_thread.stopRun();

    return TRUE;
}



/*!    $FXN :    Attach
==    ======================================================================
==    $DSC :    该函数用来注册一个处理报警信号的对象
==    $ARG :    pObj：处理报警信号的对象指针
==         :    pPorc：处理报警信号的回调函数指针
==    $RET :    BOOL
==    ======================================================================
*/
bool CAlarm::Attach(CObject * pObj, SIG_ALARM_BUFFER pProc)
{
    int iSlots;
    
    iSlots = m_sigBuffer.Attach(pObj, pProc);
    
    assert(iSlots >=0);
    
    if (iSlots < 0)
    {
        comm_errorf("alarm attach error, error num=%d!\n", iSlots);
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

bool CAlarm::Detach(CObject * pObj, SIG_ALARM_BUFFER pProc)
{
    int iSlots;
    
    iSlots = m_sigBuffer.Detach(pObj, pProc);

    if (iSlots < 0)
    {
        comm_errorf("alarm detach error!\n");
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

/*    $FXN :    SetAlarmOut
==    ======================================================================
==    $DSC :    该函数用来手动一次性对所有的联动通道刷新一遍
==    $ARG :    
==         :    
==    $RET :    
==    ======================================================================
*/
int CAlarm::SetAlarmOut(uint64 dwState, int iType)
{
    int i;
    uint64 dwOutState = 0;
    
    switch (iType)
    {
    case ALARM_OUT_AUTO:
        //只有在自动模式下可以输出
        for (i = 0; i < m_nAlarmOut; i++)
        {
            if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x0)//自动模式下
            {
                dwOutState |= (dwState & BITMSK64(i));
                m_dwManualAlarmState &= ~(BITMSK64(i));
            }
            else if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x1)//手动模式
            {
                m_dwManualAlarmState |= BITMSK64(i);
            }
            else
            {
                //关闭报警输出模式，报警输出不可用.
                m_dwManualAlarmState &= ~(BITMSK64(i));
            }                
        }        
        //m_dwAlarmOutState = dwState;
        m_pDevAlarm->SetOutState(dwOutState | m_dwManualAlarmState);
        break;
    case ALARM_OUT_MANUAL:
        m_dwManualAlarmState = dwState;
        for (i = 0; i < m_nAlarmOut; i++)
        {
            if ((m_dwManualAlarmState & BITMSK64(i)) && (!(m_dwAlarmOutState & BITMSK64(i))))
            {
                if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x0)//自动模式
                {
                    m_dwManualAlarmState &= ~(BITMSK64(i));
                }
            }
        }        
        m_pDevAlarm->SetOutState(m_dwManualAlarmState);
        break;
    case ALARM_OUT_CLOSED:        
        break;
    default:
        return -1;
    } 

	return 0;
}

uint64 CAlarm::GetAlarmOut()
{
    return m_pDevAlarm->GetOutState();
}

//获取报警事件发送时间和对应的uuid
void CAlarm::GetAlarmTime(std::string &m_time)
{
	char tmp[64]={0};
	
	SYSTEM_TIME   curTime;
	SystemGetCurrentTime(&curTime);
	sprintf(tmp,"%04d-%02d-%02d %02d:%02d:%02d",curTime.year,curTime.month,curTime.day,curTime.hour,curTime.minute,curTime.second);

	m_time = (char *)tmp;
    return ;
}

void CAlarm::GetAlarmUuid(std::string &m_uuid)
{
	char temp_uuid[256];
	uuid_t uuid;
	memset(uuid,0,sizeof(uuid));
	
	uuid_generate(uuid);
	uuid_unparse(uuid,temp_uuid);	
	m_uuid = temp_uuid;
    return ;
}

void CAlarm::onConfigAlarm(CConfigAlarm& config, int &ret)
{
	CConfigTable table_data;
    for (int i = 0; i < m_nAlarmIn; i++)
    {
        CONFIG_ALARM& cfgOld = m_CCfgAlarm.getConfig(i);
        CONFIG_ALARM& cfgNew = config.getConfig(i);
        if (memcmp(&cfgOld.hEvent, &cfgNew.hEvent, sizeof(EVENT_HANDLER)) != 0)
        {
            if (cfgNew.iSensorType == cfgOld.iSensorType)//修改常开常闭时，触发的报警信息通过报警线程来发送，不在这里发送
            {
                if ((m_dwAlarmState & BITMSK64(i)) && (cfgNew.bEnable))
                {
                    VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), i, appEventConfig, 
						&m_CCfgAlarm.getLatest(i).hEvent, &CConfigALMWorksheet::getLatest(i));    
                }
            }
            else//当正在报警联动的过程中,对配置恢复了默认,存在报警输出联动前和恢复默认后的配置不一致;导致联动输出不能停止.
            {
                if ((m_dwAlarmState & BITMSK64(i)) && (cfgNew.bEnable)
                    && ((TRUE == cfgOld.hEvent.bAlarmOutEn)))
                {                  
    				std::string sTime;
			   	    table_data.clear();
				    GetAlarmTime(sTime);
				    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
				    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stAlarmUuid[i];
					m_stAlarmUuid[i].clear();
                    VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), i, appEventStop, &m_CCfgAlarm.getConfig(i).hEvent,
                        &CConfigALMWorksheet::getLatest(i),&table_data);

                    //处理正在报警输出时,恢复默认不能停止问题
                    for (int j = 0; j < m_nAlarmOut; j++)
                    {
                        CLinkage *pLinkage = m_cAlarmOutLinkage + j;
                        
                        if ((pLinkage->stop(getEventName(appEventAlarmLocal), i)) && (pLinkage->isEmpty()) && (cfgOld.hEvent.dwAlarmOut & BITMSK(j)))
                        {
                            m_dwAlarmOutState &= ~(BITMSK64(j));
                        }
                    }
                }
            }            
        }
    }

    m_CCfgAlarm.update();
}

void CAlarm::onConfigNetAlarm(CConfigNetAlarm &config, int &ret)
{
    for (int i = 0; i < m_nNetAlarmIn; i++)
    {
        CONFIG_ALARM& cfgOld = m_CCfgNetAlarm.getConfig(i);
        CONFIG_ALARM& cfgNew = config.getConfig(i);
        if (memcmp(&cfgOld.hEvent, &cfgNew.hEvent, sizeof(EVENT_HANDLER)) != 0)
        {
            if ((m_dwNetAlarmState & BITMSK64(i)) && (cfgNew.bEnable))
            {
                VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmNet), i, appEventConfig, &cfgNew.hEvent, 
                    &CConfigNetAlmWorksheet::getLatest(i));    
            }
        }
    }
    m_CCfgNetAlarm.update();
}

//处理报警输出
void CAlarm::onAppEvent(std::string code_str, int index, appEventAction action, const EVENT_HANDLER *param, const CConfigTable* data)
{
    int i;    
    //uint m_OutState = 0;    
    //EVENT_INFO eventinfo;   
    appEventCode code = (appEventCode)getEventIndex(code_str.c_str());

    if(!param)
    {
        return;
    }   
    if ((code == appEventStorageReadErr) || (code == appEventStorageWriteErr) ||(code == appEventStorageFailure))
    {
//#ifdef HDDALM_SUPPORT_BEEP
        //硬盘出错时,进行蜂鸣提示
        if (action == appEventStart && param->bBeep)
        {
            SystemBeep(880,5000);
            SystemBeep(622,5000);        
        }

//#endif//HDDALM_SUPPORT_BEEP
        return;
    }
    
    switch(action) 
    {
    case appEventStart:
        for (i = 0; i < m_nAlarmOut; i++)
        {
            CLinkage *pLinkage = m_cAlarmOutLinkage + i;
            if ((param->bAlarmOutEn) && (param->dwAlarmOut & BITMSK(i)))
            {
                m_dwAlarmOutState |= BITMSK(i);
                pLinkage->start(code_str, index);
            }
            else
            {
                if ((pLinkage->stop(code_str, index)) && (pLinkage->isEmpty()))
                {
                    m_dwAlarmOutState &= ~(BITMSK(i));
                }
            }
        }
        SetAlarmOut(m_dwAlarmOutState);
        
        if(param->bBeep) //james.xu 090812
        {
#ifdef SHREG
            SystemBeep(880,5000);
#else
            SystemBeep(880,300);
#endif
            m_iBeepstate[index] = 1;
        }
        VF_IFrontboard::instance()->LigtenLed(FB_LED_ALARM, TRUE);

        if (0 == index)
        {
             g_Camera.SetIcrStatus(index, IMAGE_ICT_ALARM_IN, IMAGE_ICS_NIGHT);
        }
        break;
    case appEventStop:
        for (i = 0; i < m_nAlarmOut; i++)
        {
            VF_IAppEventManager::instance()->latchEvent(this, VF_IAppEventManager::Proc(&CAlarm::onAppEvent, this), code_str, index, param->iAOLatch);
        }
        VF_IFrontboard::instance()->LigtenLed(FB_LED_ALARM, FALSE);

        if (0 == index)
        {
             g_Camera.SetIcrStatus(index,IMAGE_ICT_ALARM_IN, IMAGE_ICS_DAY);
        }
        break;
    case appEventLatch:
        for(i = 0; i < m_nAlarmOut; i++)
        {
            CLinkage *pLinkage = m_cAlarmOutLinkage + i;
            ///modefied by nike.xie  2009-07-14
            ///*去除输出通道的开关校验，目的是防止某输出通道处于延迟输出状态(等待关闭)，
            ///*输出通道的开关已经被用户改变，而造成无法关闭该输出通道的报警输出
            ///*无论某输出通道的是开启还是关闭，只要该通道存在输出，就关闭该通道的输出
            ///if ((pLinkage->stop(code, index)) && (pLinkage->isEmpty()) && (param->dwAlarmOut & BITMSK(i)))
            if ((pLinkage->stop(code_str, index)) && (pLinkage->isEmpty()) )
            {
                m_dwAlarmOutState &= ~(BITMSK64(i));
            }
        }
        SetAlarmOut(m_dwAlarmOutState);
        //if(param->bBeep)
        {
            m_iBeepstate[index] = 0;
        }
        break;
    default:
        comm_warnf("appEventUnknown\n");
        break;
    }
}

void CAlarm::Alarm(uint64 dwState, int iEventType ,char *pContext)
{
    int    i = 0;    
	CConfigTable table_data;
	
    if (appEventAlarmLocal == iEventType)
    {
        for (i = 0; i < m_nAlarmIn; i++)
        {
            //本地报警开关
            if (TRUE == m_CCfgAlarm[i].bEnable)
            {    
                if (NC == m_CCfgAlarm[i].iSensorType)
                {
                    dwState ^= BITMSK64(i);
                }

                //有报警来，且本身已处于报警状态，取消报警定时器
                //!知道，报警清除到来
                if ((dwState & m_dwAlarmState )& BITMSK64(i))
                {
                	m_iAlarmLatchDelay[i] = -1;
                }
				else
				{
					if ((dwState ^ m_dwAlarmState )& BITMSK64(i))
					{
					   if (m_dwAlarmState & BITMSK64(i))
					   {
						   if(m_iAlarmLatchDelay[i] < 0)
						   {
							   m_iAlarmLatchDelay[i] = m_CCfgAlarm.getConfig(i).hEvent.iEventLatch;
						   }
					   }
					   else
					   {
						   // 设备报警输入状态
						  if(m_iAlarmLatchDelay[i] <= 0)
						   {   
						  	   std::string sTime;
						   	   table_data.clear();
							   GetAlarmTime(sTime);
							   GetAlarmUuid(m_stAlarmUuid[i]);
							   table_data["IDTDataInfoExt"][0u]["time"] = sTime;
							   table_data["IDTDataInfoExt"][0u]["uuid"] = m_stAlarmUuid[i];
							   m_dwAlarmState |= BITMSK64(i);
							   VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), i, appEventStart, &m_CCfgAlarm.getConfig(i).hEvent, 
								   &CConfigALMWorksheet::getLatest(i), &table_data);
						   }
					   }
					}
                }
            }
            else
            {
				m_iAlarmLatchDelay[i] = -1; 
				if (m_dwAlarmState & BITMSK64(i))
				{
					std::string sTime;
			   	    table_data.clear();
				    GetAlarmTime(sTime);
				    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
				    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stAlarmUuid[i];
					//本地报警结束，通知事件中心
					m_CCfgAlarm.getConfig(i).hEvent.dwAlarmOut = 0x3F;

					VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), i, appEventStop, &m_CCfgAlarm.getConfig(i).hEvent, 
						&CConfigALMWorksheet::getLatest(i), &table_data);					
					m_dwAlarmState &= ~(BITMSK64(i));					
				}		
            }            
        }
    }
    else if (appEventAlarmNet == iEventType)
    {
        int alarmin = 0;

        for (i = 0; i < m_nNetAlarmIn; i++)
        {
            //网络报警开关
            if (TRUE == m_CCfgNetAlarm[i].bEnable)
            {
                //有网络报警输入时，通知事件中心
                if ((dwState ^ m_dwNetAlarmState )& BITMSK64(i))
                {
                    if (m_dwNetAlarmState & BITMSK64(i))
                    {
						std::string sTime;
				   	    table_data.clear();
					    GetAlarmTime(sTime);
					    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
					    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stNetAlarmUuid[i];
                         m_dwNetAlarmState &= ~(BITMSK64(i));  
                        VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmNet), i, appEventStop, &m_CCfgNetAlarm.getConfig(i).hEvent, 
                            &CConfigNetAlmWorksheet::getLatest(i), &table_data);                    
                    }
                    else
                    {
                    	std::string sTime;
				   	    table_data.clear();
					    GetAlarmTime(sTime);
						GetAlarmUuid(m_stNetAlarmUuid[i]);
					    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
					    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stNetAlarmUuid[i];
                        m_dwNetAlarmState |= BITMSK64(i);                
                        alarmin = i;
                        
                        VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmNet), i, appEventStart, &m_CCfgNetAlarm.getConfig(i).hEvent,
                            &CConfigNetAlmWorksheet::getLatest(i), &table_data);       		
                    }
                }
            }
            else
            {
                if (m_dwNetAlarmState & BITMSK64(i))
                {
                	std::string sTime;
			   	    table_data.clear();
				    GetAlarmTime(sTime);
				    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
				    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stNetAlarmUuid[i];
                    m_dwNetAlarmState &= ~(BITMSK64(i));
                    //网络报警结束，通知事件中心
                    VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmNet), i, appEventStop, &m_CCfgNetAlarm.getConfig(i).hEvent, 
                        &CConfigNetAlmWorksheet::getLatest(i), &table_data);                    
                }
            }                            
        }
        
    }
}

void CAlarm::OnAlarmDelayTimer(uint arg)
{
	CConfigTable table_data;
	
	for(int iAlarmIn = 0; iAlarmIn < N_ALM_IN; iAlarmIn++)
	{
		if(m_iAlarmLatchDelay[iAlarmIn] >= 0)
		{
			if(m_iAlarmLatchDelay[iAlarmIn] == 0)
			{
				std::string sTime;
		   	    table_data.clear();
			    GetAlarmTime(sTime);
			    table_data["IDTDataInfoExt"][0u]["time"] = sTime;
			    table_data["IDTDataInfoExt"][0u]["uuid"] = m_stAlarmUuid[iAlarmIn];
				VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), iAlarmIn, appEventStop, &m_CCfgAlarm.getConfig(iAlarmIn).hEvent, 
						&CConfigALMWorksheet::getLatest(iAlarmIn), &table_data);										
				m_dwAlarmState &= ~(BITMSK64(iAlarmIn));
			}
			m_iAlarmLatchDelay[iAlarmIn]--;
		}
	}
	
	return;
}

uint64 CAlarm::GetAlarmState()
{
    return m_dwAlarmState;
}

uint64 CAlarm::GetNetAlarmState()
{
    return m_dwNetAlarmState;
}
uint CAlarm::GetAlarmPtzState(int index)
{
    if ((index >= 0) && (index < N_PTZ_ALARM))
    {
        return m_dwAlarmPtzState[index] & (~(255 << 24));
    }

    return 0;
}

uint64 CAlarm::GetDigitAlarmInState()
{
	if(m_pDigitAlarm == NULL ) return 0;

	return m_pDigitAlarm->GetAlarmState();
}

uint CAlarm::GetAlarmOutType()
{
    return m_dwAlarmOutType;    
}
int CAlarm::SetAlarmOutType(uint dwMod)
{
    m_dwAlarmOutType = dwMod;
	return 0;
}


