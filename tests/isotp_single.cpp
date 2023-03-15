#include "CppUTest/TestHarness.h"
#include "isotp.h"
#include "isotp_defines.h"
#include "isotp_mock.hpp"

#define ISOTP_CAN_ID        ( 0x700 )
#define ISOTP_BUFSIZE       ( 128 )

TEST_GROUP(ISOTP_SINGLE)
{
  /* Alloc IsoTpLink statically in RAM */
  IsoTpLink *g_link = nullptr;
  /* Alloc send and receive buffer statically in RAM */
  uint8_t g_isotpRecvBuf[ISOTP_BUFSIZE];
  uint8_t g_isotpSendBuf[ISOTP_BUFSIZE];

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

TEST(ISOTP_SINGLE, Create)
{
  CHECK(g_link != nullptr);

  /* sender paramters */
  LONGS_EQUAL( g_link->send_arbitration_id, ISOTP_CAN_ID );        /* used to reply consecutive frame */

  /* message buffer */
  CHECK( g_link->send_buffer != nullptr );
  POINTERS_EQUAL(g_link->send_buffer, &g_isotpSendBuf);
  LONGS_EQUAL( g_link->send_buf_size, sizeof(g_isotpSendBuf) );
  LONGS_EQUAL( g_link->send_size, 0 );
  LONGS_EQUAL( g_link->send_offset, 0 );

  /* multi-frame flags */
  LONGS_EQUAL( g_link->send_sn,        0 );
  LONGS_EQUAL( g_link->send_bs_remain, 0 );                        /* Remaining block size */
  LONGS_EQUAL( g_link->send_st_min_us, 0 );                        /* Separation Time between consecutive frames */
  LONGS_EQUAL( g_link->send_wtf_count, 0 );                        /* Maximum number of FC.Wait frame transmissions  */
  LONGS_EQUAL( g_link->send_timer_st,  0 );                        /* Last time send consecutive frame */
  LONGS_EQUAL( g_link->send_timer_bs,  0 );                        /* Time until reception of the next FlowControl N_PDU
                                                                   start at sending FF, CF, receive FC
                                                                   end at receive FC */
  LONGS_EQUAL( g_link->send_protocol_result, 0 );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );

  /* receiver paramters */
  LONGS_EQUAL( g_link->receive_arbitration_id, 0 );

  /* message buffer */
  CHECK( g_link->receive_buffer != nullptr );
  POINTERS_EQUAL(g_link->receive_buffer, &g_isotpRecvBuf);
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

TEST(ISOTP_SINGLE, ReceiveSingleFrame)
{
  for(uint8_t x = 1; x < 7; x++)
  {
    uint8_t single_frame[ 8 ] = { x, 0x05, 0x0A, 0x05, 0x04, 0x03, 0x05, 0x0A };

    /* Receive single frame*/
    int ret_msg_can = isotp_on_can_message(g_link, single_frame, sizeof( single_frame ));
    ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_OK );
    ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
    LONGS_EQUAL( g_link->receive_size, x );

    uint8_t payload[ 7 ] = { 0 };
    uint16_t out_size = 0;
    int ret_receive = isotp_receive(g_link, payload, sizeof(payload), &out_size);
    ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
    ENUMS_EQUAL_INT( ret_receive, ISOTP_RET_OK );
    LONGS_EQUAL(out_size, x );
    for(int i = 0; i < out_size; i++)
    {
      LONGS_EQUAL(payload[i], single_frame[i+1]);
    }

  }
}

TEST(ISOTP_SINGLE, ReceiveShortSingleFrame)
{
  const uint8_t single_frame[ 1 ] = { 0x07 };

  mock().expectOneCall("isotp_user_debug");

  /* Receive single frame*/
  int ret_msg_can = isotp_on_can_message(g_link, single_frame, sizeof( single_frame ));
  ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_LENGTH );
  ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_ERROR );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  LONGS_EQUAL( g_link->receive_size, 0 );

  uint8_t payload[ 7 ] = { 0 };
  uint16_t out_size = 0;
  int ret_receive = isotp_receive(g_link, payload, sizeof(payload), &out_size);
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  ENUMS_EQUAL_INT( ret_receive, ISOTP_RET_NO_DATA );
  LONGS_EQUAL(out_size, 0 );

  mock().checkExpectations();
}

TEST(ISOTP_SINGLE, ReceiveLongSingleFrame)
{
  const uint8_t single_frame[ 9 ] = { 0x07, 0x05, 0x0A, 0x05, 0x04, 0x03, 0x05, 0x0A, 0x0F };

  mock().expectOneCall("isotp_user_debug");

  /* Receive single frame*/
  int ret_msg_can = isotp_on_can_message(g_link, single_frame, sizeof( single_frame ));
  ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_LENGTH );
  ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_ERROR );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  LONGS_EQUAL( g_link->receive_size, 0 );

  uint8_t payload[ 7 ] = { 0 };
  uint16_t out_size = 0;
  int ret_receive = isotp_receive(g_link, payload, sizeof(payload), &out_size);
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  ENUMS_EQUAL_INT( ret_receive, ISOTP_RET_NO_DATA );
  LONGS_EQUAL(out_size, 0 );

  mock().checkExpectations();
}

TEST(ISOTP_SINGLE, RecieveBufferSmall)
{
  const uint8_t single_frame[ 8 ] = { 0x07, 0x05, 0x0A, 0x05, 0x04, 0x03, 0x05, 0x0A };

  /* Receive single frame*/
  int ret_msg_can = isotp_on_can_message(g_link, single_frame, sizeof( single_frame ));
  ENUMS_EQUAL_INT( ret_msg_can, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->receive_protocol_result, ISOTP_PROTOCOL_RESULT_OK );
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_FULL );
  LONGS_EQUAL( g_link->receive_size, sizeof( single_frame ) - 1 );

  uint8_t payload[ 5 ] = { 0 };
  uint16_t out_size = 0;
  mock().expectOneCall("isotp_user_debug");
  int ret_receive = isotp_receive(g_link, payload, sizeof(payload), &out_size);
  mock().checkExpectations();
  ENUMS_EQUAL_INT( g_link->receive_status, ISOTP_RECEIVE_STATUS_IDLE );
  ENUMS_EQUAL_INT( ret_receive, ISOTP_RET_OVERFLOW );
  LONGS_EQUAL(out_size, 0 );
}

TEST(ISOTP_SINGLE, SendSingleFrame)
{
  const uint8_t single_frame[ 7 ] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

  mock().expectOneCall("isotp_user_send_can");

  int ret = isotp_send(g_link, single_frame, sizeof( single_frame ) );
  ENUMS_EQUAL_INT( ret, ISOTP_RET_OK );
  ENUMS_EQUAL_INT( g_link->send_status, ISOTP_SEND_STATUS_IDLE );
  LONGS_EQUAL( g_link->send_size, sizeof( single_frame ) );
  LONGS_EQUAL( g_link->send_offset, 0 );
  LONGS_EQUAL( g_link->send_arbitration_id, ISOTP_CAN_ID );

  mock().checkExpectations();
}