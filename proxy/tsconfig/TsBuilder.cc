# include "TsBuilder.h"
# include <TsErrataUtil.h>
# include "TsConfigLexer.h"

// Access to terminal token values.
extern "C" {
# include "TsConfig.tab.h"
};

# define PRE "Configuration Parser: "

namespace ts { namespace config {

Builder&
Builder::init() {
    // Simple: Resize the vector and then fill in each element to
    // dispatch through the static method. Callback data is a pointer
    // to an entry in @c dispatch which contains pointer to this object
    // and a pointer to the appropriate dispatch method.

    _dispatch.resize(TS_CONFIG_N_EVENT_TYPES);

    for ( size_t i = 0 ; i < TS_CONFIG_N_EVENT_TYPES ; ++i) {
        _dispatch[i]._ptr = this;
        _handlers.handler[i]._f = &self::dispatch;
        _handlers.handler[i]._data = &(_dispatch[i]);
    }

    _dispatch[TsConfigEventGroupOpen]._method = &self::groupOpen;
    _dispatch[TsConfigEventGroupName]._method = &self::groupName;
    _dispatch[TsConfigEventGroupClose]._method = &self::groupClose;
    _dispatch[TsConfigEventListOpen]._method = &self::listOpen;
    _dispatch[TsConfigEventListClose]._method = &self::listClose;
    _dispatch[TsConfigEventPathOpen]._method = &self::pathOpen;
    _dispatch[TsConfigEventPathTag]._method = &self::pathTag;
    _dispatch[TsConfigEventPathIndex]._method = &self::pathIndex;
    _dispatch[TsConfigEventPathClose]._method = &self::pathClose;
    _dispatch[TsConfigEventLiteralValue]._method = &self::literalValue;
    _dispatch[TsConfigEventInvalidToken]._method = &self::invalidToken;

    _handlers.error._data = this;
    _handlers.error._f = &self::syntaxErrorDispatch;

    return *this;
}

// Error messages here have to just be logged, as they effectively report that
// the dispatcher can't find the builder object.
void
Builder::dispatch(void* data, Token* token) {
    if (data) {
        Handler* handler = reinterpret_cast<Handler*>(data);
        if (handler->_ptr) {
            if (handler->_method) {
                ((handler->_ptr)->*(handler->_method))(*token);
            } else {
                msg::logf(msg::WARN, PRE "Unable to dispatch event - no method.");
            }
        } else {
            msg::logf(msg::WARN, PRE "Unable to dispatch event - no builder.");
        }
    } else {
        msg::logf(msg::WARN, PRE "Unable to dispatch event - no handler.");
    }
}

int
Builder::syntaxErrorDispatch(void* data, char const* text) {
    return reinterpret_cast<Builder*>(data)->syntaxError(text);
}

int
Builder::syntaxError(char const* text) {
    msg::logf(_errata, msg::WARN, "Syntax error '%s'.", text);
    return 0;
}

Rv<Configuration>
Builder::build(Buffer const& buffer) {
    yyscan_t lexer;
    YY_BUFFER_STATE lexer_buffer_state;

    _v = _config.getRoot();
    _errata.clear();

    tsconfiglex_init(&lexer);
    tsconfigset_extra(&_handlers, lexer);
    lexer_buffer_state = tsconfig_scan_buffer(buffer._ptr, buffer._size, lexer);
    tsconfigparse(lexer, &_handlers);
    tsconfig_delete_buffer(lexer_buffer_state, lexer);
    tsconfiglex_destroy(lexer);

    return MakeRv(_config, _errata);
}

void
Builder::groupOpen(Token const&) {
    _v = _v.makeGroup(_name);
}
void Builder::groupClose(Token const&) {
    _v = _v.getParent();
}
void Builder::groupName(Token const& token) {
    _name.set(token._s, token._n);
}
void Builder::listOpen(Token const&) {
    _v = _v.makeList(_name);
}
void Builder::listClose(Token const&) {
    _v = _v.getParent();
}

void Builder::pathOpen(Token const&) {
    _path.reset();
    _extent.reset();
}
void Builder::pathTag(Token const& token) {
    _path.append(Buffer(token._s, token._n));
    if (_extent._ptr) _extent._size = token._s - _extent._ptr + token._n;
    else _extent.set(token._s, token._n);
}
void Builder::pathIndex(Token const& token){
    // We take advantage of the lexer - token will always be a valid digit string
    // that is followed by a non-digit or the FLEX required double null at the end
    // of the input buffer.
    _path.append(Buffer(0, static_cast<size_t>(atol(token._s))));
    if (_extent._ptr) _extent._size = token._s - _extent._ptr + token._n;
    else _extent.set(token._s, token._n);
}

void Builder::pathClose(Token const&) {
    Rv<Value> cv = _v.makePath(_path, _name);
    if (cv.isOK()) cv.result().setText(_extent);

    _name.reset();
    _extent.reset();
}

void Builder::literalValue(Token const& token) {
    Rv<Value> cv;
    ConstBuffer text(token._s, token._n);
    if (INTEGER == token._type) {
        cv = _v.makeInteger(text, _name);
        if (!cv.isOK()) _errata.join(cv.errata());
    } else if (STRING == token._type) {
        ++text._ptr, text._size -= 2;
        cv = _v.makeString(text, _name);
        // Strip enclosing quotes.
        if (!cv.isOK()) _errata.join(cv.errata());
    } else {
        msg::logf(_errata, msg::WARN, PRE "Unexpected literal type %d.", token._type);
    }
    _name.set(0,0); // used, so clear it.
}
void Builder::invalidToken(Token const&) { }

}} // namespace ts::config
