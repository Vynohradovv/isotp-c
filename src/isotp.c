#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include "isotp.h"

///////////////////////////////////////////////////////
///                 STATIC FUNCTIONS                ///
///////////////////////////////////////////////////////

void isotp_debug(const char* message, ...)
{
    va_list args;
	va_start( args, message );

    char debugbuff[ ISP_TP_BUFFER_DEBUG_SIZE ];
    vsnprintf(debugbuff, sizeof(debugbuff), message, args);

    isotp_user_debug(debugbuff);

    va_end( args );
}

/* st_ms to microsecond */
static uint8_t isotp_us_to_st_ms(uint32_t us) 
{
    uint32_t time_min = 0;

    if (us <= 127000) 
    {
        time_min = us / 1000;

    } else if (us >= 100000 && us <= 900000) {

        time_min = 0xF0 + (us / 100000);

    } else {

        isotp_debug("This range of values is reserved by part of ISO 15765\n");
    }

    return time_min;
}

/* st_ms to usec  */
static uint32_t isotp_st_ms_to_us(uint16_t st_ms) 
{
    uint32_t time_us = 0;

    if (st_ms <= 0x7F) 
    {
        time_us = st_ms * 1000;

    } else if (st_ms >= 0xF1 && st_ms <= 0xF9) {

        time_us = (st_ms - 0xF0) * 100000;

    } else {

        isotp_debug("This range of values is reserved by part of ISO 15765\n");
    }

    return time_us;
}

static int isotp_send_flow_control(IsoTpLink* link, uint8_t flow_status, uint8_t block_size, uint32_t st_min_us) 
{
    assert( link != NULL );

    IsoTpCanMessage message;
    int ret = ISOTP_RET_ERROR;

    /* setup message  */
    message.as.flow_control.type = ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME;
    message.as.flow_control.FS = flow_status;
    message.as.flow_control.BS = block_size;
    message.as.flow_control.STmin = isotp_us_to_st_ms(st_min_us);

    /* send message */
#if ISO_TP_FRAME_PADDING  
    (void) memset(message.as.flow_control.reserve, 0, sizeof(message.as.flow_control.reserve));
    ret = isotp_user_send_can(link->send_arbitration_id, message.as.data_array.ptr, sizeof(message));
#else    
    ret = isotp_user_send_can(link->send_arbitration_id,
            message.as.data_array.ptr,
            3);
#endif

    if( ret != ISOTP_RET_OK )
    {
        ret = ISOTP_RET_HW_NOTREADY;
        isotp_debug("The attempt to send flow control ended with an error: [ %d ]\n", ret );
    }

    return ret;
}

static int isotp_send_single_frame(IsoTpLink* link, uint32_t id) 
{
    assert( link != NULL );

    IsoTpCanMessage message;
    int ret = ISOTP_RET_ERROR;

    /* multi frame message length must greater than 7  */
    assert(link->send_size <= 7);

    /* setup message  */
    message.as.single_frame.type = ISOTP_PCI_TYPE_SINGLE;
    message.as.single_frame.SF_DL = (uint8_t) link->send_size;
    (void) memcpy(message.as.single_frame.data, link->send_buffer, link->send_size);

    /* send message */
#if ISO_TP_FRAME_PADDING
    (void) memset(message.as.single_frame.data + link->send_size, 0, sizeof(message.as.single_frame.data) - link->send_size);
    ret = isotp_user_send_can(id, message.as.data_array.ptr, sizeof(message));
#else
    ret = isotp_user_send_can(id,
            message.as.data_array.ptr,
            link->send_size + 1);
#endif

    if(ret != ISOTP_RET_OK)
    {
        ret = ISOTP_RET_HW_NOTREADY;
        isotp_debug("The attempt to send single frame ended with an error: [ %d ]\n", ret );
    }

    return ret;
}

static int isotp_send_first_frame(IsoTpLink* link, uint32_t id) 
{
    assert( link != NULL );

    IsoTpCanMessage message;
    int ret = ISOTP_RET_ERROR;

    /* multi frame message length must greater than 7  */
    assert(link->send_size > 7);

    /* setup message  */
    message.as.first_frame.type = ISOTP_PCI_TYPE_FIRST_FRAME;
    message.as.first_frame.FF_DL_low = (uint8_t) link->send_size;
    message.as.first_frame.FF_DL_high = (uint8_t) (0x0F & (link->send_size >> 8));
    (void) memcpy(message.as.first_frame.data, link->send_buffer, sizeof(message.as.first_frame.data));

    /* send message */
    ret = isotp_user_send_can(id, message.as.data_array.ptr, sizeof(message));
    if (ISOTP_RET_OK == ret) 
    {
        link->send_offset += sizeof(message.as.first_frame.data);
        link->send_sn = 1;

    } else {

        ret = ISOTP_RET_HW_NOTREADY;    
        isotp_debug("The attempt to send first frame ended with an error: [ %d ]\n", ret );
    }

    return ret;
}

static int isotp_send_consecutive_frame(IsoTpLink* link) 
{
    assert( link != NULL );

    IsoTpCanMessage message;
    uint16_t data_length;
    int ret = ISOTP_RET_ERROR;

    /* multi frame message length must greater than 7  */
    assert(link->send_size > 7);

    /* setup message  */
    message.as.consecutive_frame.type = TSOTP_PCI_TYPE_CONSECUTIVE_FRAME;
    message.as.consecutive_frame.SN = link->send_sn;
    data_length = link->send_size - link->send_offset;
    if (data_length > sizeof(message.as.consecutive_frame.data)) {
        data_length = sizeof(message.as.consecutive_frame.data);
    }
    (void) memcpy(message.as.consecutive_frame.data, link->send_buffer + link->send_offset, data_length);

    /* send message */
#if ISO_TP_FRAME_PADDING
    (void) memset(message.as.consecutive_frame.data + data_length, 0, sizeof(message.as.consecutive_frame.data) - data_length);
    ret = isotp_user_send_can(link->send_arbitration_id, message.as.data_array.ptr, sizeof(message));
#else
    ret = isotp_user_send_can(link->send_arbitration_id,
            message.as.data_array.ptr,
            data_length + 1);
#endif
    if (ISOTP_RET_OK == ret) 
    {
        link->send_offset += data_length;
        if (++(link->send_sn) > 0x0F) {
            link->send_sn = 0;
        }

    } else {

        ret = ISOTP_RET_HW_NOTREADY;    
        isotp_debug("The attempt to send consecutive frame ended with an error: [ %d ]\n", ret );
    }
    
    return ret;
}

static int isotp_receive_single_frame(IsoTpLink *link, IsoTpCanMessage *message, uint8_t len) 
{
    assert( link != NULL );
    assert( message != NULL );

    int ret = ISOTP_RET_ERROR;

    /* check data length */
    if ((0 == message->as.single_frame.SF_DL) || (message->as.single_frame.SF_DL > (len - 1))) 
    {
        ret = ISOTP_RET_LENGTH;
        isotp_debug("Single-frame length too small.\n");

    } else {

        /* copying data */
        (void) memcpy(link->receive_buffer, message->as.single_frame.data, message->as.single_frame.SF_DL);
        link->receive_size = message->as.single_frame.SF_DL;

        ret = ISOTP_RET_OK;
    }
    
    return ret;
}

static int isotp_receive_first_frame(IsoTpLink *link, IsoTpCanMessage *message, uint8_t len) 
{
    assert( link != NULL );
    assert( message != NULL );

    int ret = ISOTP_RET_ERROR;

    if (8 != len) 
    {
        ret = ISOTP_RET_LENGTH;
        isotp_debug("First frame should be 8 bytes in length.\n");

    } else {

        /* check data length */
        uint16_t payload_length = message->as.first_frame.FF_DL_high;
        payload_length = (payload_length << 8) + message->as.first_frame.FF_DL_low;

        /* should not use multiple frame transmition */
        if (payload_length <= 7) 
        {
            ret = ISOTP_RET_LENGTH;
            isotp_debug("Should not use multiple frame transmission.\n");

        } else if (payload_length > link->receive_buf_size) {

            ret = ISOTP_RET_OVERFLOW;
            isotp_debug("Multi-frame response too large for receiving buffer.\n");

        } else {
            
            /* copying data */
            (void) memcpy(link->receive_buffer, message->as.first_frame.data, sizeof(message->as.first_frame.data));
            link->receive_size = payload_length;
            link->receive_offset = sizeof(message->as.first_frame.data);
            link->receive_sn = 1;

            ret = ISOTP_RET_OK;
        }

    }

    return ret;
}

static int isotp_receive_consecutive_frame(IsoTpLink *link, IsoTpCanMessage *message, uint8_t len) 
{
    assert( link != NULL );
    assert( message != NULL );

    int ret = ISOTP_RET_ERROR;
    uint16_t remaining_bytes = 0;
    
    /* check sn */
    if (link->receive_sn != message->as.consecutive_frame.SN) 
    {
        ret = ISOTP_RET_WRONG_SN;
        isotp_debug("Wrong SN into the condecutive frame\n");

    } else {

        /* check data length */
        remaining_bytes = link->receive_size - link->receive_offset;

        if (remaining_bytes > sizeof(message->as.consecutive_frame.data)) 
        {
            remaining_bytes = sizeof(message->as.consecutive_frame.data);
        }

        if (remaining_bytes > len - 1) 
        {
            ret = ISOTP_RET_LENGTH;
            isotp_debug("Consecutive frame too short.\n");

        } else {

            /* copying data */
            (void) memcpy(link->receive_buffer + link->receive_offset, message->as.consecutive_frame.data, remaining_bytes);

            link->receive_offset += remaining_bytes;
            if (++(link->receive_sn) > 0x0F) 
            {
                link->receive_sn = 0;
            }

            ret = ISOTP_RET_OK;
        }
    }

    return ret;
}

static int isotp_receive_flow_control_frame(IsoTpLink *link, IsoTpCanMessage *message, uint8_t len) 
{
    assert( link != NULL );
    assert( message != NULL );

    int ret = ISOTP_RET_ERROR;

    /* check message length */
    if (len < 3) 
    {
        ret = ISOTP_RET_LENGTH;
        isotp_debug("Flow control frame too short.\n");

    } else {

        ret = ISOTP_RET_OK;
    }

    return ret;
}

///////////////////////////////////////////////////////
///                 PUBLIC FUNCTIONS                ///
///////////////////////////////////////////////////////

int isotp_send(IsoTpLink *link, const uint8_t payload[], uint16_t size) {
    return isotp_send_with_id(link, link->send_arbitration_id, payload, size);
}

int isotp_send_with_id(IsoTpLink *link, uint32_t id, const uint8_t payload[], uint16_t size) 
{
    assert( link != NULL );
    
    int ret = ISOTP_RET_ERROR;

    if ( link == NULL ) 
    {
        isotp_debug("Link is null!\n");
        ret = ISOTP_RET_ERROR;

    } else {

        if (size > link->send_buf_size) 
        {
            isotp_debug("Message size too large. Increase ISO_TP_MAX_MESSAGE_SIZE to set a larger buffer\n");
            isotp_debug("Attempted to send %d bytes; max size is %d!\n", size, link->send_buf_size);
            ret = ISOTP_RET_OVERFLOW;

        } else {

            if (ISOTP_SEND_STATUS_INPROGRESS == link->send_status) 
            {
                isotp_debug("Abort previous message, transmission in progress.\n");
                ret = ISOTP_RET_INPROGRESS;

            } else {

                /* copy into local buffer */
                link->send_size = size;
                link->send_offset = 0;
                link->send_arbitration_id = id;
                (void) memcpy(link->send_buffer, payload, size);

                if (link->send_size < 8) 
                {
                    /* send single frame */
                    ret = isotp_send_single_frame(link, link->send_arbitration_id);
                } else {
                    /* send multi-frame */
                    ret = isotp_send_first_frame(link, link->send_arbitration_id);

                    /* init multi-frame control flags */
                    if (ISOTP_RET_OK == ret) 
                    {
                        const uint32_t time_us = isotp_user_get_us();
                        link->send_bs_remain = 0;
                        link->send_st_min_us = 0;
                        link->send_wtf_count = 0;
                        link->send_timer_st = time_us;
                        link->send_timer_bs = time_us + ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US;
                        link->send_protocol_result = ISOTP_PROTOCOL_RESULT_OK;
                        link->send_status = ISOTP_SEND_STATUS_INPROGRESS;
                    }
                }
            }
        }
    }

    return ret;
}

int isotp_on_can_message(IsoTpLink *link, const uint8_t *data, uint8_t len) 
{
    assert( link != NULL );
    assert( data != NULL );

    IsoTpCanMessage message;
    int ret = ISOTP_RET_ERROR;
    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_ERROR;   
    
    if (len < 2 || len > 8) 
    {
       ret = ISOTP_RET_LENGTH;
       isotp_debug("Len for the msg frame not correct\n");

    } else {

        memcpy(message.as.data_array.ptr, data, len);
        memset(message.as.data_array.ptr + len, 0, sizeof(message.as.data_array.ptr) - len);

        switch (message.as.common.type) 
        {
            case ISOTP_PCI_TYPE_SINGLE: 
            {
                /* update protocol result */
                if (ISOTP_RECEIVE_STATUS_INPROGRESS == link->receive_status) 
                {
                    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_UNEXP_PDU;
                    isotp_debug("Protocol unexpect first frame\n");

                    break;
                }

                link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_OK;
                
                /* handle message */
                ret = isotp_receive_single_frame(link, &message, len);
                
                if (ISOTP_RET_OK == ret) 
                {
                    /* change status */
                    link->receive_status = ISOTP_RECEIVE_STATUS_FULL;
                    /*TODO: Callback for receive function*/
                }

                break;
            }

            case ISOTP_PCI_TYPE_FIRST_FRAME: 
            {
                /* update protocol result */
                if (ISOTP_RECEIVE_STATUS_INPROGRESS == link->receive_status) 
                {
                    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_UNEXP_PDU;
                    isotp_debug("Protocol unexpect first frame\n");

                    break;
                } 

                link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_OK;
                
                /* handle message */
                ret = isotp_receive_first_frame(link, &message, len);

                /* if overflow happened */
                if (ISOTP_RET_OVERFLOW == ret) 
                {
                    /* update protocol result */
                    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW;
                    /* change status */
                    link->receive_status = ISOTP_RECEIVE_STATUS_IDLE;
                    /* send error message */
                    ret = isotp_send_flow_control(link, PCI_FLOW_STATUS_OVERFLOW, 0, 0);
                    
                    /* if receive successful */
                } else if (ISOTP_RET_OK == ret) {

                    /* change status */
                    link->receive_status = ISOTP_RECEIVE_STATUS_INPROGRESS;
                    /* send fc frame */
                    link->receive_bs_count = ISO_TP_DEFAULT_BLOCK_SIZE;
                    ret = isotp_send_flow_control(link, PCI_FLOW_STATUS_CONTINUE, link->receive_bs_count, ISO_TP_DEFAULT_ST_MIN_MS);
                    /* refresh timer cs */
                    link->receive_timer_cr = isotp_user_get_us() + ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US;

                } else {

                    /* empty */
                }
                
                break;
            }

            case TSOTP_PCI_TYPE_CONSECUTIVE_FRAME: 
            {
                /* check if in receiving status */
                if (ISOTP_RECEIVE_STATUS_INPROGRESS != link->receive_status) 
                {
                    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_UNEXP_PDU;
                    isotp_debug("Protocol unexpect consecutive frame\n");

                    break;
                } 

                link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_OK;
                
                /* handle message */
                ret = isotp_receive_consecutive_frame(link, &message, len);

                /* if wrong sn */
                if (ISOTP_RET_WRONG_SN == ret) 
                {
                    link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_WRONG_SN;
                    link->receive_status = ISOTP_RECEIVE_STATUS_IDLE;
                    break;
                }

                /* if success */
                if (ISOTP_RET_OK == ret) 
                {
                    /* refresh timer cs */
                    link->receive_timer_cr = isotp_user_get_us() + ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US;
                    
                    /* receive finished */
                    if (link->receive_offset >= link->receive_size) 
                    {
                        link->receive_status = ISOTP_RECEIVE_STATUS_FULL;
                        /*TODO: Callback for received msg. */

                    } else {
                        /* send fc when bs reaches limit */
                        if ( 0 == --link->receive_bs_count ) 
                        {
                            link->receive_bs_count = ISO_TP_DEFAULT_BLOCK_SIZE;
                            ret = isotp_send_flow_control(link, PCI_FLOW_STATUS_CONTINUE, link->receive_bs_count, ISO_TP_DEFAULT_ST_MIN_MS);
                        }
                    }
                }
                
                break;
            }

            case ISOTP_PCI_TYPE_FLOW_CONTROL_FRAME:
            {
                /* handle fc frame only when sending in progress  */
                if (ISOTP_SEND_STATUS_INPROGRESS != link->send_status) 
                {
                    isotp_debug("Protocol unexpect flow control frame\n");

                    break;
                }

                /* handle message */
                ret = isotp_receive_flow_control_frame(link, &message, len);
                
                if (ISOTP_RET_OK == ret) 
                {
                    /* refresh bs timer */
                    link->send_timer_bs = isotp_user_get_us() + ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US;

                    /* overflow */
                    if (PCI_FLOW_STATUS_OVERFLOW == message.as.flow_control.FS) 
                    {
                        link->send_protocol_result = ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW;
                        link->send_status = ISOTP_SEND_STATUS_ERROR; /*TODO: Whot else ?*/

                        isotp_debug("Buffer in the host is overflow\n");
                    }

                    /* wait */
                    else if (PCI_FLOW_STATUS_WAIT == message.as.flow_control.FS) 
                    {
                        link->send_wtf_count += 1;
                        /* wait exceed allowed count */
                        if (link->send_wtf_count > ISO_TP_MAX_WFT_NUMBER) 
                        {
                            link->send_protocol_result = ISOTP_PROTOCOL_RESULT_WFT_OVRN;
                            link->send_status = ISOTP_SEND_STATUS_ERROR;

                            isotp_debug("The host not rady\n");
                        }
                    }

                    /* permit send */
                    else if (PCI_FLOW_STATUS_CONTINUE == message.as.flow_control.FS) 
                    {
                        if (0 == message.as.flow_control.BS) 
                        {
                            link->send_bs_remain = ISOTP_INVALID_BS;

                        } else {

                            link->send_bs_remain = message.as.flow_control.BS;
                        }

                        const uint32_t message_st_min_us = isotp_st_ms_to_us(message.as.flow_control.STmin);
                        const uint32_t user_define_st_min_us = isotp_st_ms_to_us( ISO_TP_DEFAULT_ST_MIN_MS );
                        link->send_st_min_us = message_st_min_us >  user_define_st_min_us ? message_st_min_us : user_define_st_min_us;    
                        /*TODO: Change ISO_TP_DEFAULT_ST_MIN_MS on the user frandly*/                     
                        link->send_wtf_count = 0;
                    }
                }

                break;
            }

            default:
                isotp_debug("This frame not xxx whis ISOTP protocul\n");
                break;
        };

    }
    
    return ret;
}

int isotp_receive(IsoTpLink *link, uint8_t *payload, const uint16_t payload_size, uint16_t *out_size) 
{
    assert( link != NULL );
    assert( payload != NULL );
    assert( out_size != NULL );

    int ret = ISOTP_RET_ERROR;
    uint16_t copylen = 0;
    
    if (ISOTP_RECEIVE_STATUS_FULL != link->receive_status) 
    {
        ret = ISOTP_RET_NO_DATA;

    } else {

        copylen = link->receive_size;
        if (copylen > payload_size) 
        {
            ret = ISOTP_RET_OVERFLOW; /* TODO: Small buffer size on the receiving device */
            isotp_debug("Frame response too large for receiving buffer.\n");
            
        } else {

            memcpy(payload, link->receive_buffer, copylen);
            *out_size = copylen;       

            /* TODO: Reset all receive buffers*/
            link->receive_size = 0;    
            link->receive_offset = 0;

            ret = ISOTP_RET_OK;
        }

        link->receive_status = ISOTP_RECEIVE_STATUS_IDLE;
    }

    return ret;
}

IsoTpLink* isotp_init_link(uint32_t sendid, uint8_t *sendbuf, uint16_t sendbufsize, uint8_t *recvbuf, uint16_t recvbufsize) 
{    
            
    assert(sendbuf != NULL );
    assert(recvbuf != NULL );

    IsoTpLink* link = calloc(1, sizeof(IsoTpLink));
    if( link != NULL )
    {    
        link->receive_status = ISOTP_RECEIVE_STATUS_IDLE;
        link->send_status = ISOTP_SEND_STATUS_IDLE;
        link->send_arbitration_id = sendid;
        link->send_buffer = (void *)sendbuf;
        link->send_buf_size = sendbufsize;
        link->receive_buffer = (void *)recvbuf;
        link->receive_buf_size = recvbufsize;       
        
    } else {

        isotp_debug("Initialize the ISOTP library is FAULT\n");
    }
    
    return link;
}

void isotp_poll(IsoTpLink *link) 
{
    assert( link != NULL );
   
    int ret = ISOTP_RET_ERROR;
    const uint32_t time_us = isotp_user_get_us();

    /* only polling when operation in progress */
    if (ISOTP_SEND_STATUS_INPROGRESS == link->send_status) 
    {
        /* continue send data */
        if (/* send data if bs_remain is invalid or bs_remain large than zero */
        (ISOTP_INVALID_BS == link->send_bs_remain || link->send_bs_remain > 0) &&
        /* and if st_min is zero or go beyond interval time */
        (0 == link->send_st_min_us || IsoTpTimeAfter(time_us, link->send_timer_st))) 
        {            
            ret = isotp_send_consecutive_frame(link);
            if (ISOTP_RET_OK == ret) 
            {
                if (ISOTP_INVALID_BS != link->send_bs_remain) {
                    link->send_bs_remain -= 1;
                }
                link->send_timer_bs = time_us + ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US;
                link->send_timer_st = time_us + link->send_st_min_us;

                /* check if send finish */
                if (link->send_offset >= link->send_size) {
                    link->send_status = ISOTP_SEND_STATUS_IDLE;
                }

            } else {
                link->send_status = ISOTP_SEND_STATUS_ERROR;
            }
        }

        /* check timeout */
        if (IsoTpTimeAfter(time_us, link->send_timer_bs)) {
            link->send_protocol_result = ISOTP_PROTOCOL_RESULT_TIMEOUT_BS;
            link->send_status = ISOTP_SEND_STATUS_IDLE;
        }
    }

    /* only polling when operation in progress */
    if (ISOTP_RECEIVE_STATUS_INPROGRESS == link->receive_status) 
    {        
        /* check timeout */
        if (IsoTpTimeAfter(time_us, link->receive_timer_cr)) 
        {
            link->receive_protocol_result = ISOTP_PROTOCOL_RESULT_TIMEOUT_CR;
            link->receive_status = ISOTP_RECEIVE_STATUS_IDLE;
        }
    }

    return;
}
