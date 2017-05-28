class Felica
  # Exception type for Felica protocol level errors
  class FelicaStatusError < StandardError
    class << self
      def raise!(s1, s2)
        message = sprintf('Status flag error: [%.2X:%2.X] "%s" "%s"',
                          s1, s2,
                          s1_strerror(s1), s2_strerror(s2))
        raise(new(s1, s2), message)
      end

      private

      S1_ERRORS = {
        0x00 => 'Success',
        0xFF => 'Error (no block list)',
      }.freeze

      def s1_strerror(s1)
        S1_ERRORS[s1] || 'Error (block list)'
      end

      S2_ERRORS = {
        # common
        0x00 => 'Success',
        0x01 => 'Purse data under/overflow',
        0x02 => 'Cashback data exceeded',
        0x70 => 'Memory error',
        0x71 => 'Memory warning',

        # card-specific
        0xA1 => 'Illegal Number of Service',
        0xA2 => 'Illegal command packet (specified Number of Block',
        0xA3 => 'Illegal Block List (specified order of Service',
        0xA4 => 'Illegal Service type',
        0xA5 => 'Access is not allowed',
        0xA6 => 'Illegal Service Code List',
        0xA7 => 'Illegal Block List (access mode)',
        0xA8 => 'Illegal Block Number (access to the specified data is inhibited',
        0xA9 => 'Data write failure',
        0xAA => 'Key-change failure',
        0xAB => 'Illegal Package Parity or Illegal Package MAC',
        0xAC => 'Illegal parameter',
        0xAD => 'Service exists already',
        0xAE => 'Illegal System Code',
        0xAF => 'Too many simulatenous cyclic write operations',
        0xC0 => 'Illegal Package Identifier',
        0xC1 => 'Discrepancy of parameters inside and outside Package',
        0xC2 => 'Command is disabled already',
      }.freeze

      def s2_strerror(s2)
        S2_ERRORS[s2] || 'Unknown'
      end
    end

    # @return [Fixnum] S1 error code
    attr_reader :s1

    # @return [Fixnum] S2 error code
    attr_reader :s2

    def initialize(s1, s2)
      @s1 = s1
      @s2 = s2
    end
  end
end
