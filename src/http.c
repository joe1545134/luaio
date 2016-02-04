/* Copyright © 2015 coord.cn. All rights reserved.
 * @author: QianYe(coordcn@163.com)
 * @license: MIT license
 * @overview: 
 */

#include "http_parser.h"
#include "init.h"
#include "LuaIO.h"

#define LUAIO_HTTP_DEFAULT_MAX_HEADER_LINE_SIZE (16*1024)

#define LuaIO_http_check_http_parser(L, name) \
  http_parser *parser = lua_touserdata(L, 1); \
  if (parser == NULL) { \
    return luaL_argerror(L, 1, "http_parser:"#name" error: http_parser must be [userdata](http_parser)\n"); \
  }

#define LuaIO_http_check_buffer(L, name) \
  LuaIO_buffer_t *buffer = lua_touserdata(L, 2); \
  if (buffer == NULL || buffer->type != LUAIO_TYPE_READ_BUFFER) { \
    return luaL_argerror(L, 2, "http_parser:"#name" error: buffer must be [ReadBuffer]\n"); \
  }

static char LuaIO_http_parser_metatable_key;

/* @example: local parser = http.new_parser(max_header_line_size) */
static int LuaIO_http_new_parser(lua_State *L) {
  lua_Integer size = luaL_optinteger(L, 1, LUAIO_HTTP_DEFAULT_MAX_HEADER_LINE_SIZE);
  if (size < 0) {
    return luaL_argerror(L, 1, "http.new_parser(max_header_line_size) error: max_header_line_size must be >= 0\n");
  }

  http_parser *parser = lua_newuserdata(L, sizeof(http_parser));
  if (parser == NULL) {
    lua_pushnil(L);
    return 1;
  }

  http_parser_init(parser);
  parser->max_header_line_size = (uint32_t)size;

  lua_pushlightuserdata(L, &LuaIO_http_parser_metatable_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  lua_setmetatable(L, -2);
  return 1;
}

/* @example: local status, http_major, http_minor, error = http_parser:parse_status_line(read_buffer)
 * @param: read_buffer {userdate(ReadBuffer)}
 * @return: status {integer}
 * @return: http_major {integer}
 * @return: http_minor {integer}
 * @return: error {integer} 
 */
static int LuaIO_http_parser_parse_status_line(lua_State *L) {
  LuaIO_http_check_http_parser(L, parse_status_line(buffer));
  LuaIO_http_check_buffer(L, parse_status_line(buffer));

  int rest_size, dist;
  char *start, *last_pos;
  char *read_pos = buffer->read_pos;
  char *write_pos = buffer->write_pos;
  int ret = http_parse_status_line(parser, read_pos, write_pos);
  last_pos = parser->last_pos;

  if (ret == HTTP_OK) {
    lua_pushinteger(L, parser->status_code);
    lua_pushinteger(L, parser->http_major);
    lua_pushinteger(L, parser->http_minor);
    lua_pushinteger(L, ret);

    if (last_pos == write_pos) {
      start = buffer->start;
      buffer->read_pos = start;
      buffer->write_pos = start;
    } else {
      buffer->read_pos = last_pos;
    }
    parser->last_pos = NULL;

    return 4;
  }
    
  if (ret == HTTP_AGAIN && write_pos == buffer->end) {
    start = buffer->start;

    /* start == read_pos   write_pos == end
     * |                           |
     * +++++++++++++++++++++++++++++
     */
    if (read_pos == start) {
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushinteger(L, HTTP_ERROR);
      return 4;
    }

    /* start  read_pos   write_pos == end
     * |        |                   |
     * ---------+++++++++++++++++++++
     */
    rest_size = write_pos - read_pos;
    dist = read_pos - start;

    LuaIO_memmove(start, read_pos, rest_size);
    buffer->read_pos = start;
    buffer->write_pos = start + rest_size;
    parser->last_pos = last_pos - dist;
  }
      
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushinteger(L, ret);
  return 4;
}

/* @example: local method, http_major, http_minor, url, error = http_parser:parse_request_line(read_buffer)
 * @param: read_buffer {userdate(ReadBuffer)}
 * @return: method {integer}
 * @return: http_major {integer}
 * @return: http_minor {integer}
 * @return: url {table} @http_parser:parse_request_line
 *    local url = {
 *      schema = {string}
 *      auth = {string}
 *      host = {string}
 *      port = {string}
 *      path = {string}
 *      query = {string}
 *      hash = {string}
 *    }
 * @return: error {integer} 
 */
static int LuaIO_http_parser_parse_request_line(lua_State *L) {
  LuaIO_http_check_http_parser(L, parse_request_line(buffer));
  LuaIO_http_check_buffer(L, parse_request_line(buffer));

  int rest_size, dist;
  char *start, *last_pos;
  char *read_pos = buffer->read_pos;
  char *write_pos = buffer->write_pos;
  int ret = http_parse_request_line(parser, read_pos, write_pos);
  last_pos = parser->last_pos;

  if (ret == HTTP_OK) {
    http_url *url = &parser->url;
    char *server = url->server.base;
    size_t length = url->server.len;
    if (server && length > 0) {
      ret = http_parse_host(url, server, length, parser->found_at);
      if (ret) {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushinteger(L, HTTP_BAD_REQUEST);
        return 5;
      }
    }

    lua_pushinteger(L, parser->method);
    lua_pushinteger(L, parser->http_major);
    lua_pushinteger(L, parser->http_minor);
    lua_createtable(L, 0, 7);
    if (url->schema.base != NULL) {
      LuaIO_setlstring("schema", url->schema.base, url->schema.len)
    }

    if (url->userinfo.base != NULL) {
      LuaIO_setlstring("auth", url->userinfo.base, url->userinfo.len)
    }

    if (url->host.base != NULL) {
      LuaIO_setlstring("host", url->host.base, url->host.len)
    }

    if (url->port.base != NULL) {
      LuaIO_setlstring("port", url->port.base, url->port.len)
    }

    if (url->path.base != NULL) {
      LuaIO_setlstring("path", url->path.base, url->path.len)
    }

    if (url->query.base != NULL) {
      LuaIO_setlstring("query", url->query.base, url->query.len)
    }

    if (url->fragment.base != NULL) {
      LuaIO_setlstring("hash", url->fragment.base, url->fragment.len)
    }
    lua_pushinteger(L, ret);

    if (last_pos == write_pos) {
      start = buffer->start;
      buffer->read_pos = start;
      buffer->write_pos = start;
    } else {
      buffer->read_pos = last_pos;
    }
    parser->last_pos = NULL;

    return 5;
  }
    
  if (ret == HTTP_AGAIN && write_pos == buffer->end) {
    start = buffer->start;

    /* start == read_pos   write_pos == end
     * |                           |
     * +++++++++++++++++++++++++++++
     */
    if (read_pos == start) {
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushinteger(L, HTTP_REQUEST_URI_TOO_LARGE);
      return 5;
    }

    /* start  read_pos   write_pos == end
     * |        |                   |
     * ---------+++++++++++++++++++++
     */
    rest_size = write_pos - read_pos;
    dist = read_pos - start;

    LuaIO_memmove(start, read_pos, rest_size);
    buffer->read_pos = start;
    buffer->write_pos = start + rest_size;
    parser->last_pos = last_pos - dist;
    http_url *url = &parser->url;
    if (url->schema.base != NULL) {
      url->schema.base -= dist;
    }

    if (url->server.base != NULL) {
      url->server.base -= dist;
    }

    if (url->path.base != NULL) {
      url->path.base -= dist;
    }

    if (url->query.base != NULL) {
      url->query.base -= dist;
    }

    if (url->fragment.base != NULL) {
      url->fragment.base -= dist;
    }
  }
      
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushinteger(L, ret);
  return 5;
}

static int LuaIO_http_parser_parse_headers(lua_State *L) {
  LuaIO_http_check_http_parser(L, parse_headers(buffer));
  LuaIO_http_check_buffer(L, parse_headers(buffer));

  http_buf_t *buf;
  int rest_size;
  int nbuf = 0;
  char *start, *last_pos;
  char *read_pos = buffer->read_pos;
  char *write_pos = buffer->write_pos;
  int ret = http_parse_headers(parser, read_pos, write_pos);
  last_pos = parser->last_pos;

  if (ret == HTTP_OK || ret == HTTP_DONE) {
    nbuf = (int)parser->nbuf;
    lua_createtable(L, 0, nbuf);
    for (int i = 0; i < nbuf; i++) {
      buf = &(parser->headers[i]);
      lua_pushlstring(L, buf->base, buf->len);
      lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, nbuf);
    lua_pushinteger(L, ret);

    if (last_pos == write_pos) {
      start = buffer->start;
      buffer->read_pos = start;
      buffer->write_pos = start;
    } else {
      buffer->read_pos = last_pos;
    }
    parser->last_pos = NULL;

    return 3;
  }
    
  if (ret == HTTP_AGAIN && write_pos == buffer->end) {
    start = buffer->start;

    /* start == read_pos   write_pos == end
     * |                           |
     * +++++++++++++++++++++++++++++
     */
    if (parser->nbuf == 0 && read_pos == start) {
      lua_pushnil(L);
      lua_pushnil(L);
      lua_pushinteger(L, HTTP_BAD_REQUEST);
      return 3;
    }

    nbuf = (int)parser->nbuf;
    if (nbuf > 0) {
      lua_createtable(L, 0, nbuf);
      for (int i = 0; i < nbuf; i++) {
        buf = &(parser->headers[i]);
        lua_pushlstring(L, buf->base, buf->len);
        lua_rawseti(L, -2, i + 1);
      }
      lua_pushinteger(L, nbuf);
    }

    /* start  read_pos   write_pos == end
     * |        |                   |
     * ---------+++++++++++++++++++++
     */
    if (parser->index == 0) { 
      buffer->read_pos = start;
      buffer->write_pos = start;
      parser->last_pos = NULL;
    } else {
      read_pos = parser->headers[nbuf].base;
      rest_size = write_pos - read_pos;
      LuaIO_memmove(start, read_pos, rest_size);
      buffer->read_pos = start;
      write_pos = start + rest_size;
      buffer->write_pos = write_pos;
      parser->last_pos = write_pos;
    }
  }
   
  if (nbuf == 0 ) {
    lua_pushnil(L);
    lua_pushnil(L);
  }
  lua_pushinteger(L, ret);
  return 3;
}

static int LuaIO_http_parser_reset(lua_State *L) {
  LuaIO_http_check_http_parser(L, reset());
  http_parser_init(parser);
  return 0;
}

static int LuaIO_http_parse_url(lua_State *L) {
  http_url url;
  size_t len;
  char *str = (char*)luaL_checklstring(L, 1, &len);

  int ret = http_parse_url(&url, str, len);
  if (ret) {
    lua_pushnil(L);
  } else {
    lua_createtable(L, 0, 7);
    if (url.schema.base != NULL) {
      LuaIO_setlstring("schema", url.schema.base, url.schema.len)
    }

    if (url.userinfo.base != NULL) {
      LuaIO_setlstring("auth", url.userinfo.base, url.userinfo.len)
    }

    if (url.host.base != NULL) {
      LuaIO_setlstring("host", url.host.base, url.host.len)
    }

    if (url.port.base != NULL) {
      LuaIO_setlstring("port", url.port.base, url.port.len)
    }

    if (url.path.base != NULL) {
      LuaIO_setlstring("path", url.path.base, url.path.len)
    }

    if (url.query.base != NULL) {
      LuaIO_setlstring("query", url.query.base, url.query.len)
    }

    if (url.fragment.base != NULL) {
      LuaIO_setlstring("hash", url.fragment.base, url.fragment.len)
    }
  }

  return 1;
}

static void LuaIO_http_setup_constants(lua_State *L) {
#define XX(num, name, _) \
  lua_pushinteger(L, num); \
  lua_setfield(L, -2, #name);

  HTTP_METHOD_MAP(XX)
#undef XX

  lua_pushinteger(L, HTTP_OK);
  lua_setfield(L, -2, "OK");

  lua_pushinteger(L, HTTP_DONE);
  lua_setfield(L, -2, "DONE");

  lua_pushinteger(L, HTTP_AGAIN);
  lua_setfield(L, -2, "AGAIN");

  lua_pushinteger(L, HTTP_ERROR);
  lua_setfield(L, -2, "ERROR");
}

int luaopen_http(lua_State *L) {
  /*http_Parser metatable*/
  lua_pushlightuserdata(L, &LuaIO_http_parser_metatable_key);

  lua_createtable(L, 0, 4);
  LuaIO_function(LuaIO_http_parser_parse_status_line, "parse_status_line")
  LuaIO_function(LuaIO_http_parser_parse_request_line, "parse_request_line")
  LuaIO_function(LuaIO_http_parser_parse_headers, "parse_headers")
  LuaIO_function(LuaIO_http_parser_reset, "reset")

  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");

  /*save http_Parser metatable*/
  lua_rawset(L, LUA_REGISTRYINDEX);

  luaL_Reg lib[] = {
    { "new_parser", LuaIO_http_new_parser },
    { "parse_url", LuaIO_http_parse_url },
    { "__newindex", LuaIO_cannot_change },
    { NULL, NULL }
  };

  lua_createtable(L, 0, 0);

  luaL_newlib(L, lib);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pushliteral(L, "metatable is protected.");
  lua_setfield(L, -2, "__metatable");
  LuaIO_http_setup_constants(L);

  lua_setmetatable(L, -2);

  return 1;
}