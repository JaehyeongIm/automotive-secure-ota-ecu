/* 첫 퍼징 하니스 — 서명 이미지 헤더 파서를 공격한다.
 *
 * 타깃: ota_img_header_read(const uint8_t *image_start, OTA_ImgHeader_t *out)
 *   - OTA로 전달된 펌웨어 이미지의 맨 앞 헤더(16B)를 읽는 순수 파서.
 *   - 이 바이트들은 공격자(게이트웨이/CAN)가 통제하는 입력이다 → 퍼징 대상.
 *
 * libFuzzer는 이 함수를 수만 번, 매번 다른 (data, size)로 호출한다.
 *   data = 퍼저가 만든 바이트뭉치, size = 그 길이.
 */
#include "ota_meta.h"
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* 가드(precondition): 이 파서는 '최소 헤더 크기 이상'의 버퍼를 가정한다.
       실제 호출부(uds.c)는 플래시 슬롯 포인터를 넘기므로 항상 충분한 바이트가 있다.
       그보다 짧은 입력의 over-read는 취약점이 아니라 계약 위반(F-001/B)이므로
       여기서 걸러내고, '진짜 파싱 로직(magic 검사)'만 퍼징한다. */
    if (size < sizeof(OTA_ImgHeader_t)) return 0;

    OTA_ImgHeader_t out;
    ota_img_header_read(data, &out);
    return 0;
}
