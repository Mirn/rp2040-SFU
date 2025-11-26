/*
 * sfu_commands.h
 *
 *  Created on: 08 ���� 2016 �.
 *      Author: Easy
 */

#ifndef SFU_COMMANDS_H_
#define SFU_COMMANDS_H_

void sfu_command_init();
void sfu_command_parser(uint8_t code, uint8_t *body, uint32_t size);
void sfu_command_timeout();

void main_start();

extern bool main_selector;
extern bool main_update_started;
bool find_latest_variant(bool *variant);

#endif /* SFU_COMMANDS_H_ */
