#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "isotp.h"
#include "isotp_defines.h"

#define ISOTP_CAN_ID        ( 0x700 )
#define ISOTP_BUFSIZE       ( 128 )

TEST_GROUP(ISOTP_MULTIPLE)
{
  /* Alloc IsoTpLink statically in RAM */
  IsoTpLink *g_link = nullptr;
  /* Alloc send and receive buffer statically in RAM */
  uint8_t g_isotpRecvBuf[ISOTP_BUFSIZE];
  uint8_t g_isotpSendBuf[ISOTP_BUFSIZE]; 

  const uint8_t first_multi_frame[ 8 ] = { 0x10, 0x0A, 0x0A, 0x05, 0x04, 0x03, 0x0A, 0x05 };
  const uint8_t second_multi_frame[ 5 ] = { 0x21, 0x0A, 0x0A, 0x05, 0x04 };

  const uint8_t send_multi_frame[ 10 ] = { 0x0A, 0x05, 0x04, 0x03, 0x0A, 0x05, 0x01, 0x08, 0x0F, 0x0A };
  const uint8_t receive_flow_frame[ 3 ] = { 0x30, 0x03, 0x0A }; /* TODO: Check time send*/

  void setup()
  {
    /* Initialize link, ISOTP_CAN_ID is the CAN ID you send with */
    g_link = isotp_init_link(ISOTP_CAN_ID,
						g_isotpSendBuf, sizeof(g_isotpSendBuf), 
						g_isotpRecvBuf, sizeof(g_isotpRecvBuf));           
  }
  void teardown()
  {
    free( g_link );  
    mock().clear();   
  }  
};

TEST(ISOTP_MULTIPLE, Create)
{
  CHECK(g_link != nullptr);

  /* sender paramters */
  LONGS_EQUAL( g_link->send_arbitration_id, ISOTP_CAN_ID );        /* used to reply consecutive frame */

  /* message buffer */
  CHECK( g_link->send_buffer != nullptr );                         
  LONGS_EQUAL( g_link->send_buf_size, sizeof(g_isotpSendBuf) );
  LONGS_EQUAL( g_link->send_size, 0 );
  LONGS_EQUAL( g_link->send_offset, 0 );

  /* multi-frame flags */
  LONGS_EQUAL( g_link->send_sn,        0 );
  LONGS_EQUAL( g_link->send_bs_remain, 0 );                        /* Remaining block size */
  LONGS_EQUAL( g_link->send_st_min_us, 0 );                        /* Separation Time between consecutive frames */
  LONGS_EQUAL( g_link->send_wtf_count, 0 );                        /* Maximum number of FC.Wait frame transmissions  */
  LONGS_EQUAL( g_link->send_timer_st,  0 );                         /* Last time send consecutive frame */  
  LONGS_EQUAL( g_link->send_timer_bs,  0 );                         /* Time until reception of the next FlowControl N_PDU
                                                                   start at sending FF, CF, receive FC
                                                                   end at receive FC */
  LONGS_EQUAL( g_link->send_protocol_result, 0 );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );

  /* receiver paramters */
  LONGS_EQUAL( g_link->receive_arbitration_id, 0 );

  /* message buffer */
  CHECK( g_link->receive_buffer != nullptr );                      
  LONGS_EQUAL( g_link->receive_buf_size, sizeof(g_isotpRecvBuf));
  LONGS_EQUAL( g_link->receive_size,   0 );
  LONGS_EQUAL( g_link->receive_offset, 0 );
  
  /* multi-frame control */
  LONGS_EQUAL( g_link->receive_sn,       0 );
  LONGS_EQUAL( g_link->receive_bs_count, 0 );                      /* Maximum number of FC.Wait frame transmissions  */
  LONGS_EQUAL( g_link->receive_timer_cr, 0 );                      /* Time until transmission of the next ConsecutiveFrame N_PDU
                                                                   start at sending FC, receive CF 
                                                                   end at receive FC */
  LONGS_EQUAL( g_link->receive_protocol_result, 0 );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );  
}

TEST(ISOTP_MULTIPLE, ReceiveMultiFrame)
{    
    mock().expectOneCall("isotp_user_send_can");
    mock().expectNCalls(4,"isotp_user_get_us");

    /* Receive multi frame*/ 
    int ret_first_msg_can = isotp_on_can_message(g_link, first_multi_frame, sizeof( first_multi_frame )); 

    ENUMS_EQUAL_INT( ret_first_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS );
    LONGS_EQUAL( g_link->receive_size, first_multi_frame[1] );
    LONGS_EQUAL( g_link->receive_offset, 6 );    

    isotp_poll( g_link );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

    /* Receive multi frame*/ 
    int ret_second_msg_can = isotp_on_can_message(g_link, second_multi_frame, sizeof( second_multi_frame )); 

    ENUMS_EQUAL_INT( ret_second_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
    LONGS_EQUAL( g_link->receive_size, 10 );
    LONGS_EQUAL( g_link->receive_offset, 10 );  

    isotp_poll( g_link );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

    uint16_t out_size = 0;
    uint8_t payload[ 10 ] = { 0 };
    int ret_outbuf = isotp_receive(g_link, payload, sizeof( payload ), &out_size);
    ENUMS_EQUAL_INT( ret_outbuf, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
    LONGS_EQUAL( out_size, 10 );
    LONGS_EQUAL( g_link->receive_size, 0 );
    LONGS_EQUAL( g_link->receive_offset, 0 );

    mock().checkExpectations();
    
}

TEST(ISOTP_MULTIPLE, SendMultiFrame)
{

  mock().expectNCalls( 2, "isotp_user_send_can" );
  mock().expectNCalls( 4, "isotp_user_get_us" );   

  int ret = isotp_send(g_link, send_multi_frame, sizeof( send_multi_frame ));

  ENUMS_EQUAL_INT( ret, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->send_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

  LONGS_EQUAL( g_link->send_offset,     6 );
  LONGS_EQUAL( g_link->send_bs_remain,  0 );
  LONGS_EQUAL( g_link->send_st_min_us,  0 );
  LONGS_EQUAL( g_link->send_wtf_count,  0 ); 
  LONGS_EQUAL( g_link->send_size, sizeof( send_multi_frame ));

  isotp_poll( g_link );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );  

  /* Receive flow frame*/ 
  int ret_msg_can = isotp_on_can_message(g_link, receive_flow_frame, sizeof( receive_flow_frame )); 
  ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->send_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
  LONGS_EQUAL( g_link->send_wtf_count, 0 );

  isotp_poll( g_link );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  LONGS_EQUAL( g_link->send_offset,     10 );
  LONGS_EQUAL( g_link->send_sn,          2 );
    
  mock().checkExpectations();

}

TEST(ISOTP_MULTIPLE, SendMultiFrameTimeOut)
{

  mock().expectNCalls( 3, "isotp_user_send_can");
  mock().expectNCalls( 20006, "isotp_user_get_us" );   

  /* -------------------------- Send No ACK -------------------------- */ 

  int ret = isotp_send(g_link, send_multi_frame, sizeof( send_multi_frame ));

  ENUMS_EQUAL_INT( ret, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->send_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

  LONGS_EQUAL( g_link->send_offset,     6 );
  LONGS_EQUAL( g_link->send_bs_remain,  0 );
  LONGS_EQUAL( g_link->send_st_min_us,  0 );
  LONGS_EQUAL( g_link->send_wtf_count,  0 ); 
  LONGS_EQUAL( g_link->send_size, sizeof( send_multi_frame ));
  
  while( g_link->send_status != ISOTP_SEND_STATUS_IDLE )
  {    
    isotp_poll( g_link );

    LONGS_EQUAL( g_link->send_offset,     6 );
    LONGS_EQUAL( g_link->send_bs_remain,  0 );
    LONGS_EQUAL( g_link->send_st_min_us,  0 );
    LONGS_EQUAL( g_link->send_wtf_count,  0 ); 
    LONGS_EQUAL( g_link->send_size, sizeof( send_multi_frame ));

    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );  
  }

  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  
  /* --------------------------- Send multi frame -------------------- */ 

  ret = isotp_send(g_link, send_multi_frame, sizeof( send_multi_frame ));

  ENUMS_EQUAL_INT( ret, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->send_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

  LONGS_EQUAL( g_link->send_offset,     6 );
  LONGS_EQUAL( g_link->send_bs_remain,  0 );
  LONGS_EQUAL( g_link->send_st_min_us,  0 );
  LONGS_EQUAL( g_link->send_wtf_count,  0 ); 
  LONGS_EQUAL( g_link->send_size, sizeof( send_multi_frame ));

  isotp_poll( g_link );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );  

  /* Receive flow frame*/ 
  int ret_msg_can = isotp_on_can_message(g_link, receive_flow_frame, sizeof( receive_flow_frame )); 
  ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_INPROGRESS );
  ENUMS_EQUAL_INT( g_link->send_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
  LONGS_EQUAL( g_link->send_wtf_count, 0 );

  isotp_poll( g_link );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  LONGS_EQUAL( g_link->send_offset,     10 );
  LONGS_EQUAL( g_link->send_sn,          2 );
    
  mock().checkExpectations();
}

TEST(ISOTP_MULTIPLE, ReceiveMultiFrameTimeOut)
{    
    mock().expectNCalls( 2, "isotp_user_send_can");
    mock().expectNCalls( 20006,"isotp_user_get_us");

    /* -------------------------- Receive No Second Frame -------------------------- */ 

    /* Receive multi frame*/ 
    int ret_first_msg_can = isotp_on_can_message(g_link, first_multi_frame, sizeof( first_multi_frame )); 

    ENUMS_EQUAL_INT( ret_first_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS );
    LONGS_EQUAL( g_link->receive_size, first_multi_frame[1] );
    LONGS_EQUAL( g_link->receive_offset, 6 );    

    while( g_link->receive_status != ISOTP_RECEIVE_STATUS_IDLE )
    {
      isotp_poll( g_link );
    }

    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_TIMEOUT_CR );

    /* -------------------------- Receive Multi Frame -------------------------- */ 

    /* Receive multi frame*/ 
    ret_first_msg_can = isotp_on_can_message(g_link, first_multi_frame, sizeof( first_multi_frame )); 

    ENUMS_EQUAL_INT( ret_first_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS );
    LONGS_EQUAL( g_link->receive_size, first_multi_frame[1] );
    LONGS_EQUAL( g_link->receive_offset, 6 );    

    isotp_poll( g_link );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_INPROGRESS );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

    /* Receive multi frame*/ 
    int ret_second_msg_can = isotp_on_can_message(g_link, second_multi_frame, sizeof( second_multi_frame )); 

    ENUMS_EQUAL_INT( ret_second_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
    LONGS_EQUAL( g_link->receive_size, 10 );
    LONGS_EQUAL( g_link->receive_offset, 10 );  

    isotp_poll( g_link );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );

    uint16_t out_size = 0;
    uint8_t payload[ 10 ] = { 0 };
    int ret_outbuf = isotp_receive(g_link, payload, sizeof( payload ), &out_size);
    ENUMS_EQUAL_INT( ret_outbuf, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
    LONGS_EQUAL( out_size, 10 );
    LONGS_EQUAL( g_link->receive_size, 0 );
    LONGS_EQUAL( g_link->receive_offset, 0 );
    
    mock().checkExpectations();
}

/*TODO: isotp_on_can_message():
        - consecutive frame
        - flow frame
        - first frame
        - single frame
        
        
        isotp_st_ms_to_us();
        isotp_us_to_st_min();
        */