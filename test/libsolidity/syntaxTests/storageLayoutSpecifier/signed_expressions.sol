int constant SIGNED2 = 2;
int constant SIGNED2_NEGATIVE = -2;
uint constant UNSIGNED2 = 2;

contract A layout at SIGNED2 * 1 {}
contract B layout at SIGNED2_NEGATIVE * -1 {}
contract C layout at SIGNED2_NEGATIVE * SIGNED2_NEGATIVE {}
contract D layout at 2 * -1 * -1 {}
contract E layout at 2 * SIGNED2 {}
contract F layout at 2 * -1 * SIGNED2_NEGATIVE {}
// ----
