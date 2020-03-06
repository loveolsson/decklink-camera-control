#include "decklink.h"
#include "mutexfifo.h"
#include "printpacket.h"
#include "serialoutput.h"
#include "defines.h"              // for Packet, Header, PADDING

#include <stdint.h>               // for uint8_t
#include <stdlib.h>               // for EXIT_FAILURE, EXIT_SUCCESS, size_t
#include <sys/signal.h>           // for signal, SIGINT
#include <exception>              // for exception
#include <string>                 // for stoi
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>

static volatile int keepRunning = 1;

static void intHandler(int)
{
	keepRunning = 0;
}

static void PrintUsage()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "\t./DeckLinkCameraControl /dev/ttyS0 19200 \"DeckLink device\"" << std::endl
			  << std::endl;
	std::cout << "\tSerial device (/dev/ttyS[] on Linux, /dev/cu.[] on Mac)." << std::endl;
	std::cout << "\tBaud rate [Optional], defaults to 19200." << std::endl;
	std::cout << "\tDeckLink device [Optional], defaults to first device." << std::endl
			  << std::endl;
}

int main(int argc, char *argv[])
{
	signal(SIGINT, intHandler);

	int baudRate = 19200;

	if (argc < 2 || argc > 4)
	{
		PrintUsage();
		return EXIT_FAILURE;
	}

	if (argc > 2)
	{
		try
		{
			baudRate = std::stoi(argv[2]);
		}
		catch (const std::exception &)
		{
			std::cout << "Could not parse baud rate. Exiting..." << std::endl;
			PrintUsage();
			return EXIT_FAILURE;
		}
	}

	SerialOutput serialOutput(argv[1], baudRate);
	if (!serialOutput.Begin())
	{
		std::cout << "Failed to open serial device: " << argv[1] << std::endl;
		PrintUsage();
		return EXIT_FAILURE;
	}

	std::cout << "Searching for DeckLink cards..." << std::endl;

	const char* deckLinkName = argc == 4 ? argv[3] : "";
	auto wDeckLinkInput = GetDeckLinkByNameOrFirst(deckLinkName);
	if (!wDeckLinkInput)
	{
		std::cout << "Found no DeckLink cards... exiting." << std::endl;
		PrintUsage();
		return EXIT_FAILURE;
	}

	ByteFifo fifo;
	DeckLinkReceiver receiver(wDeckLinkInput, fifo);
	Packet pkt;

	while (keepRunning)
	{
		bool gotJobDone = false;

		if (fifo.Pop((uint8_t *)&pkt.header, sizeof(Header)) != 0)
		{
			if (fifo.Pop((uint8_t *)&pkt.commandInfo, PADDING(pkt.header.len)) != 0)
			{
				gotJobDone = true;

				if (pkt.header.dest == 1 || pkt.header.dest == 255) {
					const size_t totalLength = sizeof(Header) + pkt.header.len;
					serialOutput.Write((uint8_t *)&pkt, totalLength);
					PrintPacket(pkt);
				}
			}
			else
			{
				// If a header is written to the fifo, the full message should also be available
				std::cout << "Not enough data in fifo, exiting..." << std::endl;
				return EXIT_FAILURE;
			}
		}

		if (!gotJobDone)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}

	std::cout << std::endl
			  << "Exiting..." << std::endl;

	return EXIT_SUCCESS;
}
