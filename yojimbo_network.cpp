/*
    Yojimbo Client/Server Network Library.
    
    Copyright © 2016, The Network Protocol Company, Inc.

    All rights reserved.
*/

#include "yojimbo_network.h"

#if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS

    #define NOMINMAX
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <ws2ipdef.h>
    #pragma comment( lib, "WS2_32.lib" )

    #ifdef SetPort
    #undef SetPort
    #endif // #ifdef SetPort

#elif YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_MAC || YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_UNIX

    #include <netdb.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    
#else

    #error network2 unknown platform!

#endif

#include <memory.h>
#include <string.h>

namespace yojimbo
{
    static bool s_networkInitialized = false;

    bool InitializeNetwork()     
    {         
        assert( !s_networkInitialized );

        bool result = true;

#if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        WSADATA WsaData;         
        result = WSAStartup( MAKEWORD(2,2), &WsaData ) == NO_ERROR;
#endif // #if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS

        if ( result )
            s_networkInitialized = result;   

        return result;
    }

    void ShutdownNetwork()
    {
        assert( s_networkInitialized );

#if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        WSACleanup();
#endif // #if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS

        s_networkInitialized = false;
    }

    bool IsNetworkInitialized()
    {
        return s_networkInitialized;
    }

    Address::Address()
    {
        Clear();
    }

    Address::Address( uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port )
    {
        m_type = ADDRESS_IPV4;
        m_address_ipv4 = uint32_t(a) | (uint32_t(b)<<8) | (uint32_t(c)<<16) | (uint32_t(d)<<24);
        m_port = port;
    }

    Address::Address( uint32_t address, int16_t port )
    {
        m_type = ADDRESS_IPV4;
        m_address_ipv4 = htonl( address );        // IMPORTANT: stored in network byte order. eg. big endian!
        m_port = port;
    }

    Address::Address( uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                      uint16_t e, uint16_t f, uint16_t g, uint16_t h,
                      uint16_t port )
    {
        m_type = ADDRESS_IPV6;
        m_address_ipv6[0] = htons( a );
        m_address_ipv6[1] = htons( b );
        m_address_ipv6[2] = htons( c );
        m_address_ipv6[3] = htons( d );
        m_address_ipv6[4] = htons( e );
        m_address_ipv6[5] = htons( f );
        m_address_ipv6[6] = htons( g );
        m_address_ipv6[7] = htons( h );
        m_port = port;
    }

    Address::Address( const uint16_t address[], uint16_t port )
    {
        m_type = ADDRESS_IPV6;
        for ( int i = 0; i < 8; ++i )
            m_address_ipv6[i] = htons( address[i] );
        m_port = port;
    }

    Address::Address( const sockaddr_storage & addr )
    {
        if ( addr.ss_family == AF_INET )
        {
            const sockaddr_in & addr_ipv4 = reinterpret_cast<const sockaddr_in&>( addr );
            m_type = ADDRESS_IPV4;
            m_address_ipv4 = addr_ipv4.sin_addr.s_addr;
            m_port = ntohs( addr_ipv4.sin_port );
        }
        else if ( addr.ss_family == AF_INET6 )
        {
            const sockaddr_in6 & addr_ipv6 = reinterpret_cast<const sockaddr_in6&>( addr );
            m_type = ADDRESS_IPV6;
            memcpy( m_address_ipv6, &addr_ipv6.sin6_addr, 16 );
            m_port = ntohs( addr_ipv6.sin6_port );
        }
        else
        {
            assert( false );
            Clear();
        }
    }

    Address::Address( const sockaddr_in6 & addr_ipv6 )
    {
        m_type = ADDRESS_IPV6;
        memcpy( m_address_ipv6, &addr_ipv6.sin6_addr, 16 );
        m_port = ntohs( addr_ipv6.sin6_port );
    }

    Address::Address( addrinfo * p )
    {
        m_port = 0;
        if ( p->ai_family == AF_INET )
        { 
            m_type = ADDRESS_IPV4;
            struct sockaddr_in * ipv4 = (struct sockaddr_in *)p->ai_addr;
            m_address_ipv4 = ipv4->sin_addr.s_addr;
            m_port = ntohs( ipv4->sin_port );
        } 
        else if ( p->ai_family == AF_INET6 )
        { 
            m_type = ADDRESS_IPV6;
            struct sockaddr_in6 * ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            memcpy( m_address_ipv6, &ipv6->sin6_addr, 16 );
            m_port = ntohs( ipv6->sin6_port );
        }
        else
        {
            Clear();
        }
    }

    Address::Address( const char * address )
    {
        Parse( address );
    }

    Address::Address( const char * address, uint16_t port )
    {
        Parse( address );
        m_port = port;
    }

    void Address::Parse( const char * address_in )
    {
        // first try to parse as an IPv6 address:
        // 1. if the first character is '[' then it's probably an ipv6 in form "[addr6]:portnum"
        // 2. otherwise try to parse as raw IPv6 address, parse using inet_pton

        assert( address_in );

        char buffer[256];
        char * address = buffer;
        strncpy( address, address_in, 255 );
        address[255] = '\0';

        int addressLength = (int) strlen( address );
        m_port = 0;
        if ( address[0] == '[' )
        {
            const int base_index = addressLength - 1;
            for ( int i = 0; i < 6; ++i )   // note: no need to search past 6 characters as ":65535" is longest port value
            {
                const int index = base_index - i;
                if ( index < 3 )
                    break;
                if ( address[index] == ':' )
                {
                    m_port = uint16_t( atoi( &address[index + 1] ) );
                    address[index-1] = '\0';
                }
            }
            address += 1;
        }
        struct in6_addr sockaddr6;
        if ( inet_pton( AF_INET6, address, &sockaddr6 ) == 1 )
        {
            memcpy( m_address_ipv6, &sockaddr6, 16 );
            m_type = ADDRESS_IPV6;
            return;
        }

        // otherwise it's probably an IPv4 address:
        // 1. look for ":portnum", if found save the portnum and strip it out
        // 2. parse remaining ipv4 address via inet_pton

        addressLength = (int) strlen( address );
        const int base_index = addressLength - 1;
        for ( int i = 0; i < 6; ++i )   // note: no need to search past 6 characters as ":65535" is longest port value
        {
            const int index = base_index - i;
            if ( index < 0 )
                break;
            if ( address[index] == ':' )
            {
                m_port = (uint16_t) atoi( &address[index+1] );
                address[index] = '\0';
            }
        }

        struct sockaddr_in sockaddr4;
        if ( inet_pton( AF_INET, address, &sockaddr4.sin_addr ) == 1 )
        {
            m_type = ADDRESS_IPV4;
            m_address_ipv4 = sockaddr4.sin_addr.s_addr;
        }
        else
        {
            // nope: it's not an IPv4 address. maybe it's a hostname? set address as undefined.
            Clear();
        }
    }

    void Address::Clear()
    {
        m_type = ADDRESS_UNDEFINED;
        memset( m_address_ipv6, 0, sizeof( m_address_ipv6 ) );
        m_port = 0;
    }

    uint32_t Address::GetAddress4() const
    {
        assert( m_type == ADDRESS_IPV4 );
        return m_address_ipv4;
    }

    const uint16_t * Address::GetAddress6() const
    {
        assert( m_type == ADDRESS_IPV6 );
        return m_address_ipv6;
    }

    void Address::SetPort( uint16_t port )
    {
        m_port = port;
    }

    uint16_t Address::GetPort() const 
    {
        return m_port;
    }

    AddressType Address::GetType() const
    {
        return m_type;
    }

    const char * Address::ToString( char buffer[], int bufferSize ) const
    {
        if ( m_type == ADDRESS_IPV4 )
        {
            const uint8_t a =   m_address_ipv4 & 0xff;
            const uint8_t b = ( m_address_ipv4 >> 8  ) & 0xff;
            const uint8_t c = ( m_address_ipv4 >> 16 ) & 0xff;
            const uint8_t d = ( m_address_ipv4 >> 24 ) & 0xff;
            if ( m_port != 0 )
                snprintf( buffer, bufferSize, "%d.%d.%d.%d:%d", a, b, c, d, m_port );
            else
                snprintf( buffer, bufferSize, "%d.%d.%d.%d", a, b, c, d );
            return buffer;
        }
        else if ( m_type == ADDRESS_IPV6 )
        {
            if ( m_port == 0 )
            {
                inet_ntop( AF_INET6, (void*) &m_address_ipv6, buffer, bufferSize );
                return buffer;
            }
            else
            {
                char addressString[INET6_ADDRSTRLEN];
                inet_ntop( AF_INET6, (void*) &m_address_ipv6, addressString, INET6_ADDRSTRLEN );
                snprintf( buffer, bufferSize, "[%s]:%d", addressString, m_port );
                return buffer;
            }
        }
        else
        {
            return "undefined";
        }
    }

    bool Address::IsValid() const
    {
        return m_type != ADDRESS_UNDEFINED;
    }

    bool Address::operator ==( const Address & other ) const
    {
        if ( m_type != other.m_type )
            return false;
        if ( m_port != other.m_port )
            return false;
        if ( m_type == ADDRESS_IPV4 && m_address_ipv4 == other.m_address_ipv4 )
            return true;
        else if ( m_type == ADDRESS_IPV6 && memcmp( m_address_ipv6, other.m_address_ipv6, sizeof( m_address_ipv6 ) ) == 0 )
            return true;
        else
            return false;
    }

    bool Address::operator !=( const Address & other ) const
    {
        return !( *this == other );
    }

#if YOJIMBO_SOCKETS

    Socket::Socket( uint16_t port, SocketType type )
    {
        assert( IsNetworkInitialized() );

        m_error = SOCKET_ERROR_NONE;

        // create socket

        m_socket = socket( ( type == SOCKET_TYPE_IPV6 ) ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP );

        if ( m_socket <= 0 )
        {
            printf( "create socket failed: %s\n", strerror( errno ) );
            m_error = SOCKET_ERROR_CREATE_FAILED;
            return;
        }

        // force IPv6 only if necessary

        if ( type == SOCKET_TYPE_IPV6 )
        {
            int yes = 1;
            if ( setsockopt( m_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&yes, sizeof(yes) ) != 0 )
            {
                printf( "failed to set ipv6 only sockopt\n" );
                m_error = SOCKET_ERROR_SOCKOPT_IPV6_ONLY_FAILED;
                return;
            }
        }

        // bind to port

        if ( type == SOCKET_TYPE_IPV6 )
        {
            sockaddr_in6 sock_address;
            memset( &sock_address, 0, sizeof( sockaddr_in6 ) );
            sock_address.sin6_family = AF_INET6;
            sock_address.sin6_addr = in6addr_any;
            sock_address.sin6_port = htons( port );

            if ( ::bind( m_socket, (const sockaddr*) &sock_address, sizeof(sock_address) ) < 0 )
            {
                printf( "bind socket failed (ipv6) - %s\n", strerror( errno ) );
                m_socket = SOCKET_ERROR_BIND_IPV6_FAILED;
                return;
            }
        }
        else
        {
            sockaddr_in sock_address;
            sock_address.sin_family = AF_INET;
            sock_address.sin_addr.s_addr = INADDR_ANY;
            sock_address.sin_port = htons( port );

            if ( ::bind( m_socket, (const sockaddr*) &sock_address, sizeof(sock_address) ) < 0 )
            {
                printf( "bind socket failed (ipv4) - %s\n", strerror( errno ) );
                m_error = SOCKET_ERROR_BIND_IPV4_FAILED;
                return;
            }
        }

        // todo: get the actual port we bound to in the case of passing in port 0 we must ask the OS

        m_port = port;

        // set non-blocking io

        #if NETWORK2_PLATFORM == NETWORK2_PLATFORM_MAC || NETWORK2_PLATFORM == NETWORK2_PLATFORM_UNIX
    
            int nonBlocking = 1;
            if ( fcntl( m_socket, F_SETFL, O_NONBLOCK, nonBlocking ) == -1 )
            {
                printf( "failed to make socket non-blocking\n" );
                m_error = SOCKET_ERROR_SET_NON_BLOCKING_FAILED;
                return;
            }
        
        #elif NETWORK2_PLATFORM == NETWORK2_PLATFORM_WINDOWS
    
            DWORD nonBlocking = 1;
            if ( ioctlsocket( m_socket, FIONBIO, &nonBlocking ) != 0 )
            {
                printf( "failed to make socket non-blocking\n" );
                m_error = SOCKET_ERROR_SET_NON_BLOCKING_FAILED;
                return;
            }

        #else

            #error unsupported platform

        #endif
    }

    Socket::~Socket()
    {
        if ( m_socket != 0 )
        {
            #if NETWORK2_PLATFORM == NETWORK2_PLATFORM_MAC || NETWORK2_PLATFORM == NETWORK2_PLATFORM_UNIX
            close( m_socket );
            #elif NETWORK2_PLATFORM == NETWORK2_PLATFORM_WINDOWS
            closesocket( m_socket );
            #else
            #error unsupported platform
            #endif
            m_socket = 0;
        }
    }

    bool Socket::IsError() const
    {
        return m_error != SOCKET_ERROR_NONE;
    }

    int Socket::GetError() const
    {
        return m_error;
    }

    bool Socket::SendPacket( const Address & address, const void * packetData, size_t packetBytes )
    {
        assert( packetData );
        assert( packetBytes > 0 );
        assert( address.IsValid() );
        assert( m_socket );
        assert( !IsError() );

        bool result = false;

        if ( address.GetType() == ADDRESS_IPV6 )
        {
            sockaddr_in6 socket_address;
            memset( &socket_address, 0, sizeof( socket_address ) );
            socket_address.sin6_family = AF_INET6;
            socket_address.sin6_port = htons( address.GetPort() );
            memcpy( &socket_address.sin6_addr, address.GetAddress6(), sizeof( socket_address.sin6_addr ) );
            size_t sent_bytes = sendto( m_socket, (const char*)packetData, (int) packetBytes, 0, (sockaddr*)&socket_address, sizeof( sockaddr_in6 ) );
            result = sent_bytes == packetBytes;
        }
        else if ( address.GetType() == ADDRESS_IPV4 )
        {
            sockaddr_in socket_address;
            memset( &socket_address, 0, sizeof( socket_address ) );
            socket_address.sin_family = AF_INET;
            socket_address.sin_addr.s_addr = address.GetAddress4();
            socket_address.sin_port = htons( (unsigned short) address.GetPort() );
            size_t sent_bytes = sendto( m_socket, (const char*)packetData, (int) packetBytes, 0, (sockaddr*)&socket_address, sizeof(sockaddr_in) );
            result = sent_bytes == packetBytes;
        }

        if ( !result )
        {
            printf( "sendto failed: %s\n", strerror( errno ) );
        }

        return result;
    }

    int Socket::ReceivePacket( Address & from, void * packetData, int maxPacketSize )
    {
        assert( m_socket );
        assert( packetData );
        assert( maxPacketSize > 0 );

#if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        typedef int socklen_t;
#endif // #if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        
        sockaddr_storage sockaddr_from;
        socklen_t fromLength = sizeof( sockaddr_from );

        int result = recvfrom( m_socket, (char*)packetData, maxPacketSize, 0, (sockaddr*)&sockaddr_from, &fromLength );

#if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        if ( result == SOCKET_ERROR )
        {
            int error = WSAGetLastError();

            if ( error == WSAEWOULDBLOCK )
                return 0;

            printf( "recvfrom failed: %d\n", error );

            return 0;
        }
#else // #if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS
        if ( result <= 0 )
        {
            if ( errno == EAGAIN )
                return 0;

            printf( "recvfrom failed: %s\n", strerror( errno ) );

            return 0;
        }
#endif // #if YOJIMBO_PLATFORM == YOJIMBO_PLATFORM_WINDOWS

        from = Address( sockaddr_from );

        assert( result >= 0 );

        const int bytesRead = result;

        return bytesRead;
    }

#endif // #if YOJIMBO_SOCKETS
}