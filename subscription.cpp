#include <getopt.h>
#include <cups/cups.h>

#include <iostream>

int main(int argc, char **argv)
{
    int c = 0;
    char *port = nullptr;
    while ((c = getopt(argc, argv, "p:")) != -1) {
        switch (c) {
        case 'p':
            port = optarg;
            break;
        default:
            std::cerr << "Error argument!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (!port) {
        std::cerr << "Set recipient port!\n";
        return EXIT_FAILURE;
    }

    http_t* http = httpConnect2(
        "localhost", ippPort(), nullptr, AF_UNSPEC, cupsEncryption(), 1, 30000, nullptr);

    if (!http) {
        std::cerr << "Cannot connect to CUPS!\n";
        return EXIT_FAILURE;
    }

    ipp_t *request = ippNewRequest(IPP_CREATE_PRINTER_SUBSCRIPTION);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
        "attributes-charset", NULL, "utf-8");

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
        "attributes-natural-language", NULL, "en");

    std::string printer_uri = "http://localhost:" + std::to_string(ippPort());
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
        "printer-uri", nullptr, printer_uri.c_str());

    std::string recipient_uri = std::string("rss://localhost:") + port;
    ippAddString(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
        "notify-recipient-uri", nullptr, recipient_uri.c_str());

    const char * const events[] = {
        "printer-added",
        "printer-deleted"
    };

    ippAddStrings(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
        "notify-events", sizeof (events) / sizeof (events[0]), nullptr, events);

    ipp_t* response = cupsDoRequest(http, request, "/");

    if (!response) {
        std::cerr << "Error in cupsDoRequest!\n";
        return EXIT_FAILURE;
    }

    ipp_attribute_t* attr = nullptr;
    if ((attr = ippFindAttribute(response, "notify-subscription-id", IPP_TAG_INTEGER)) != nullptr) {
        const int notify_subscription_id = ippGetInteger(attr, 0);
        std::cout << "Notify subscription id = " << notify_subscription_id << '\n';
    }
    else {
        std::cerr << "Error in subscription!\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
