
#include "Alarm.h"
#include "DevAlarm.h"
#include "APIs/Alarm.h"
#include "Manager/ILog.h"
#include "Comm/VF_Frontboard.h"
#include "Comm/ConfigAlarm.h"
#include "CommDefs.h"
#include "uuid/uuid.h"
#include "Media/ICamera.h"

#define NLEVER 6
#define CALARM_ALL_CHANNELS -1
#define ALARMIN_LATCH_DELAY 0    //����������ʱʱ���Ĭ��ֵ

extern SYSTEM_TIME         g_StartTime;            //����ʱ��
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
// �õ���������·��
int IAlarmManager::getInChannels()
{
	return CDevAlarm::GetInSlots();
}
// �õ��������·��
int IAlarmManager::getOutChannels()
{
	return CDevAlarm::GetOutSlots();
}


/*-----------------------------------------------------------------------
    CAlarm��ľ�̬��Ա����������Ϊ��̬����ΪCAlarmManager������һ��ʼ��
    ��ʼ�������о�Ҫ�����ã�
-----------------------------------------------------------------------*/
PATTERN_SINGLETON_IMPLEMENT(CAlarm);
void CAlarm::ThreadProc(uint arg)
{
    while (m_thread.getLoop())                //��ȡ���ַ����ֱ����ź�
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

    m_dwAlarmOutType = 0;    //�������ģʽ��ʼ��Ϊ�Զ����.

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
	
    //��ȡ������Ϣ
    m_CCfgAlarm.update();
    m_CCfgNetAlarm.update();

    //���¼�����ע�ᴦ����
    VF_IAppEventManager::instance()->attach(VF_IAppEventManager::Proc(&CAlarm::onAppEvent, this));

    //���ñ仯�ص�����
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
    ��������ģ���е��̺߳Ͷ�ʱ��
-----------------------------------------------------------------------*/
	m_cAlarmTimer.Start(this, (VD_TIMERPROC)&CAlarm::OnAlarmDelayTimer, 0, 1000);
   
    /*-----------------------------------------------------------------------
        ��������ģ���е��̺߳Ͷ�ʱ��
    -----------------------------------------------------------------------*/
	m_thread.run("Alarm", this, (ASYNPROC)&CAlarm::ThreadProc, 0, 0);
    return TRUE;
}

bool CAlarm::Stop()
{
    comm_infof("CAlarm::Stop()>>>>>>>>>\n");
    
/*-----------------------------------------------------------------------
    �����̺߳�ֹͣ��ʱ��
-----------------------------------------------------------------------*/
    m_thread.stopRun();

    return TRUE;
}



/*!    $FXN :    Attach
==    ======================================================================
==    $DSC :    �ú�������ע��һ���������źŵĶ���
==    $ARG :    pObj���������źŵĶ���ָ��
==         :    pPorc���������źŵĻص�����ָ��
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
==    $DSC :    �ú��������ֶ�һ���Զ����е�����ͨ��ˢ��һ��
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
        //ֻ�����Զ�ģʽ�¿������
        for (i = 0; i < m_nAlarmOut; i++)
        {
            if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x0)//�Զ�ģʽ��
            {
                dwOutState |= (dwState & BITMSK64(i));
                m_dwManualAlarmState &= ~(BITMSK64(i));
            }
            else if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x1)//�ֶ�ģʽ
            {
                m_dwManualAlarmState |= BITMSK64(i);
            }
            else
            {
                //�رձ������ģʽ���������������.
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
                if (((m_dwAlarmOutType >> (2 * i)) & 0x3) == 0x0)//�Զ�ģʽ
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

//��ȡ�����¼�����ʱ��Ͷ�Ӧ��uuid
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
            if (cfgNew.iSensorType == cfgOld.iSensorType)//�޸ĳ�������ʱ�������ı�����Ϣͨ�������߳������ͣ��������﷢��
            {
                if ((m_dwAlarmState & BITMSK64(i)) && (cfgNew.bEnable))
                {
                    VF_IAppEventManager::instance()->notify(getEventName(appEventAlarmLocal), i, appEventConfig, 
						&m_CCfgAlarm.getLatest(i).hEvent, &CConfigALMWorksheet::getLatest(i));    
                }
            }
            else//�����ڱ��������Ĺ�����,�����ûָ���Ĭ��,���ڱ����������ǰ�ͻָ�Ĭ�Ϻ�����ò�һ��;���������������ֹͣ.
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

                    //�������ڱ������ʱ,�ָ�Ĭ�ϲ���ֹͣ����
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

//���������
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
        //Ӳ�̳���ʱ,���з�����ʾ
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
            ///*ȥ�����ͨ���Ŀ���У�飬Ŀ���Ƿ�ֹĳ���ͨ�������ӳ����״̬(�ȴ��ر�)��
            ///*���ͨ���Ŀ����Ѿ����û��ı䣬������޷��رո����ͨ���ı������
            ///*����ĳ���ͨ�����ǿ������ǹرգ�ֻҪ��ͨ������������͹رո�ͨ�������
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
            //���ر�������
            if (TRUE == m_CCfgAlarm[i].bEnable)
            {    
                if (NC == m_CCfgAlarm[i].iSensorType)
                {
                    dwState ^= BITMSK64(i);
                }

                //�б��������ұ����Ѵ��ڱ���״̬��ȡ��������ʱ��
                //!֪���������������
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
						   // �豸��������״̬
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
					//���ر���������֪ͨ�¼�����
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
            //���籨������
            if (TRUE == m_CCfgNetAlarm[i].bEnable)
            {
                //�����籨������ʱ��֪ͨ�¼�����
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
                    //���籨��������֪ͨ�¼�����
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


