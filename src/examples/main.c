/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/console.h>

clog_config g_clog;


int main(int argc, char* argv[])
{
    g_clog.log = clog_console;

    return 0;
}