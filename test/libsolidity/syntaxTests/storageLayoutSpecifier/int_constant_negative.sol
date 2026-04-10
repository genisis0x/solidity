int constant INT256 = -42;
int8 constant INT8 = -64;
int constant EXPRESSION = INT256 * 2;
int constant COMPLEX = EXPRESSION * -2 + EXPRESSION * 4;
contract A layout at INT256 {}
contract B layout at INT8 {}
contract C layout at EXPRESSION {}
contract D layout at COMPLEX {}
// ----
// TypeError 6753: (169-175): The base slot of the storage layout evaluates to -42, which is outside the range of type uint256.
// TypeError 6753: (200-204): The base slot of the storage layout evaluates to -64, which is outside the range of type uint256.
// TypeError 6753: (229-239): The base slot of the storage layout evaluates to -84, which is outside the range of type uint256.
// TypeError 6753: (264-271): The base slot of the storage layout evaluates to -168, which is outside the range of type uint256.
