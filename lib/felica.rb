##
# Felica device access.
#
# Wraps a minimal amount of libnfc and provides methods specific to
# felica targets. The main entrypoint to consuming this module is
# Felica::Felica.

module Felica
  ##
  # Wrapper class to simplify the construction of Felica::Device. All
  # instances share a single context.
  #
  #     require 'felica'
  #     f = Felica::Felica.new
  #     loop do
  #       card = f.poll
  #       ..
  #     end

  class Felica
    ##
    # Constructor. Initialises the underlying NFC device.

    def initialize
      @device = ::Felica.open_device
      @device.init!
    end

    ##
    # Block until a card is detected, returns a Felica::Device.

    def poll
      @device.select_felica
    end
  end

  private

  def self.init!
    @@context = NFC.make_context
  end

  def self.open_device
    @@context.open_device
  end
end

require 'felica/errors'
require 'felica/felica'

Felica::init!
