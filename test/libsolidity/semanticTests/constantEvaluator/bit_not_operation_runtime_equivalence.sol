uint8 constant U253 = 253; // 1111 1101
int8 constant I128 = -128; // 1000 0000
int8 constant IONE = 1; // 0000 0001
contract C {
    uint8 constant UNSIGNED = ~U253; // = 2 (0000 0010)
    uint[UNSIGNED] a;
    int8 constant NEGATIVE_SIGNED = ~I128; // = 127 (0111 1111)
    uint[NEGATIVE_SIGNED] b;
    int8 constant POSITIVE_SIGNED = ~IONE; // = -2 (1111 1110)
    uint[POSITIVE_SIGNED * -1] c;
    function testUnsignedEquivalence() public view returns (bool) {
        uint8 runTimeResult = ~U253;

            return
                UNSIGNED == runTimeResult &&
                a.length == runTimeResult;
    }
    function testNegativeSignedEquivalence() public view returns (bool) {
        int8 runTimeResult = ~I128;

        return
            NEGATIVE_SIGNED == runTimeResult &&
            b.length == 127;
    }
    function testPositiveSignedEquivalence() public view returns (bool) {
        int8 runTimeResult = ~IONE;

        return
            POSITIVE_SIGNED == runTimeResult &&
            c.length == 2;
    }
}
// ----
// testUnsignedEquivalence() -> true
// testNegativeSignedEquivalence() -> true
// testPositiveSignedEquivalence() -> true
