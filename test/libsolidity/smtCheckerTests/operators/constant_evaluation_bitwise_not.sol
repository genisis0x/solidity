contract C {
    uint256 constant largeConstant = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF;
    function test() public pure returns (uint256) {
        return ~largeConstant;
    }
}
// ====
// SMTEngine: chc
// ----
// Warning 6031: (186-199): Internal error: Expression undefined for SMT solver.
